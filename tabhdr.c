#include "tabhdr.h"

#include <cairo.h>
#include <drm_fourcc.h>
#include <glib-object.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>

struct TabTextBuffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
};

struct TabFontMetrics {
	int logical_height;
};

static const struct TabFontMetrics *tabhdr_font_metrics(void);
static char *tabhdr_transform_title(const char *title);
static struct wlr_scene_buffer *tabhdr_create_text_node(struct wlr_scene_tree *parent, const char *title, int width,
		int height, float scale, const float color[static 4]);
static bool tabhdr_ignore_input(struct wlr_scene_buffer *buffer, double *sx, double *sy);
static void tabhdrclear(VirtualOutput *vout);

static bool
tabhdr_ignore_input(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	(void)sx;
	(void)sy;
	return false;
}

static void
tabhdr_text_buffer_destroy(struct wlr_buffer *buffer)
{
	struct TabTextBuffer *tab = wl_container_of(buffer, tab, base);
	cairo_surface_destroy(tab->surface);
	free(tab);
}

static bool
tabhdr_text_buffer_begin(struct wlr_buffer *buffer, uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
	struct TabTextBuffer *tab = wl_container_of(buffer, tab, base);
	(void)flags;
	*data = cairo_image_surface_get_data(tab->surface);
	*stride = cairo_image_surface_get_stride(tab->surface);
	*format = DRM_FORMAT_ARGB8888;
	return true;
}

static void
tabhdr_text_buffer_end(struct wlr_buffer *buffer)
{
	(void)buffer;
}

static const struct wlr_buffer_impl tabhdr_text_buffer_impl = {
		.destroy = tabhdr_text_buffer_destroy,
		.begin_data_ptr_access = tabhdr_text_buffer_begin,
		.end_data_ptr_access = tabhdr_text_buffer_end,
};

static const struct TabFontMetrics *
tabhdr_font_metrics(void)
{
	static struct TabFontMetrics metrics;
	static int initialized;
	const struct TabHdrStyle *style = tabhdr_style();

	if (!initialized) {
		cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cairo_t *cr = cairo_create(surface);
		PangoLayout *layout = NULL;
		PangoFontDescription *font = NULL;
		PangoRectangle logical = {0};

		if (cr)
			layout = pango_cairo_create_layout(cr);
		if (layout)
			font = pango_font_description_from_string(style->font);

		if (layout && font) {
			pango_layout_set_font_description(layout, font);
			pango_layout_set_single_paragraph_mode(layout, true);
			pango_layout_set_text(layout, "Ag", -1);
			pango_layout_get_pixel_extents(layout, NULL, &logical);
			metrics.logical_height = logical.height;
		}

		if (font)
			pango_font_description_free(font);
		if (layout)
			g_object_unref(layout);
		if (cr)
			cairo_destroy(cr);
		if (surface)
			cairo_surface_destroy(surface);
		initialized = 1;
	}

	return &metrics;
}

int
tabhdr_header_height(void)
{
	const struct TabHdrStyle *style = tabhdr_style();
	const struct TabFontMetrics *fm = tabhdr_font_metrics();
	int content = fm->logical_height;

	if (content < 0)
		content = 0;
	return style->padding_top + content + style->padding_bottom;
}

enum TabHdrPos
tabhdr_position_value(void)
{
	return tabhdr_style()->position;
}

static char *
tabhdr_transform_title(const char *title)
{
	const struct TabHdrStyle *style = tabhdr_style();
	const TabTitleTransformRule *rule;
	char *current;
	bool changed = false;

	if (!title)
		title = "";
	if (!style->title_transforms[0].pattern)
		return NULL;

	current = strdup(title);
	if (!current)
		return NULL;

	for (rule = style->title_transforms; rule->pattern; rule++) {
		regex_t regex;
		const char *replacement;
		size_t repl_len;
		char *input;
		bool rule_changed;

		if (regcomp(&regex, rule->pattern, REG_EXTENDED))
			continue;
		replacement = rule->replacement ? rule->replacement : "";
		repl_len = strlen(replacement);
		input = current;
		rule_changed = false;

		while (1) {
			regmatch_t match;
			size_t prefix_len, suffix_len, new_len;
			char *temp;
			int ret = regexec(&regex, input, 1, &match, 0);
			if (ret != 0)
				break;
			prefix_len = match.rm_so;
			suffix_len = strlen(input) - match.rm_eo;
			new_len = prefix_len + repl_len + suffix_len + 1;
			temp = ecalloc(new_len, sizeof(char));
			memcpy(temp, input, prefix_len);
			memcpy(temp + prefix_len, replacement, repl_len);
			memcpy(temp + prefix_len + repl_len, input + match.rm_eo, suffix_len + 1);
			free(input);
			input = temp;
			rule_changed = true;
		}

		regfree(&regex);
		current = input;
		if (rule_changed)
			changed = true;
	}

	if (!changed) {
		free(current);
		return NULL;
	}

	return current;
}

static struct wlr_scene_buffer *
tabhdr_create_text_node(struct wlr_scene_tree *parent, const char *title, int width, int height, float scale,
		const float color[static 4])
{
	const struct TabHdrStyle *style = tabhdr_style();
	cairo_surface_t *surface = NULL;
	cairo_t *cr = NULL;
	PangoLayout *layout = NULL;
	PangoFontDescription *font = NULL;
	struct TabTextBuffer *buffer = NULL;
	struct wlr_scene_buffer *node = NULL;
	int text_width, surf_w, surf_h, text_y, max_y;
	int top_pad = style->padding_top;
	int bottom_pad = style->padding_bottom;
	int left_pad = style->padding_left;
	int right_pad = style->padding_right;
	int content_top;
	PangoRectangle logical = {0};

	if (!parent || !title || !*title || width <= 0 || height <= 0)
		return NULL;
	if (scale <= 0.0f)
		scale = 1.0f;
	text_width = width - left_pad - right_pad;
	if (text_width <= 0)
		return NULL;
	surf_w = (int)ceilf(width * scale);
	surf_h = (int)ceilf(height * scale);
	if (surf_w <= 0 || surf_h <= 0)
		return NULL;
	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surf_w, surf_h);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
		goto cleanup;
	cr = cairo_create(surface);
	if (!cr)
		goto cleanup;
	cairo_scale(cr, scale, scale);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	layout = pango_cairo_create_layout(cr);
	if (!layout)
		goto cleanup;
	font = pango_font_description_from_string(style->font);
	if (!font)
		goto cleanup;
	pango_layout_set_font_description(layout, font);
	pango_layout_set_single_paragraph_mode(layout, true);
	pango_layout_set_width(layout, text_width * PANGO_SCALE);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_text(layout, title, -1);
	pango_layout_get_pixel_extents(layout, NULL, &logical);
	content_top = top_pad;
	text_y = content_top - logical.y;
	if (text_y < 0)
		text_y = 0;
	max_y = height - bottom_pad - logical.height;
	if (max_y < 0)
		max_y = 0;
	if (text_y > max_y)
		text_y = max_y;
	cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
	cairo_move_to(cr, left_pad, text_y);
	pango_cairo_show_layout(cr, layout);
	cairo_surface_flush(surface);
	pango_font_description_free(font);
	font = NULL;
	g_object_unref(layout);
	layout = NULL;
	cairo_destroy(cr);
	cr = NULL;

	buffer = ecalloc(1, sizeof(*buffer));
	if (!buffer)
		goto cleanup;
	buffer->surface = surface;
	wlr_buffer_init(&buffer->base, &tabhdr_text_buffer_impl, surf_w, surf_h);
	node = wlr_scene_buffer_create(parent, &buffer->base);
	if (!node) {
		wlr_buffer_drop(&buffer->base);
		buffer = NULL;
		surface = NULL;
		goto cleanup;
	}
	node->point_accepts_input = tabhdr_ignore_input;
	wlr_scene_buffer_set_dest_size(node, width, height);
	wlr_buffer_drop(&buffer->base);
	buffer = NULL;
	surface = NULL;

cleanup:
	if (font)
		pango_font_description_free(font);
	if (layout)
		g_object_unref(layout);
	if (cr)
		cairo_destroy(cr);
	if (!node && surface)
		cairo_surface_destroy(surface);
	return node;
}

static void
tabhdrclear(VirtualOutput *vout)
{
	struct wlr_scene_node *node, *tmp;

	if (!vout || !vout->tabhdr)
		return;
	wl_list_for_each_safe(node, tmp, &vout->tabhdr->children, link) wlr_scene_node_destroy(node);
}

void
tabhdr_disable(VirtualOutput *vout)
{
	if (!vout || !vout->tabhdr)
		return;
	tabhdrclear(vout);
	wlr_scene_node_set_enabled(&vout->tabhdr->node, 0);
}

void
tabhdr_update(Monitor *m, VirtualOutput *vout, struct wlr_box area, Client *active)
{
	const struct TabHdrStyle *style = tabhdr_style();
	struct wlr_scene_tree *tree;
	Client *c;
	int count = 0;
	int height;
	int idx = 0;
	int base_width, remainder;
	int x = 0;
	float scale = 1.0f;
	bool active_is_virtual_fs;

	if (!vout || !(tree = vout->tabhdr))
		return;
	height = tabhdr_header_height();
	if (height <= 0 || area.width <= 0 || area.height <= height || !vout->ws) {
		tabhdr_disable(vout);
		return;
	}

	wl_list_for_each(c, &clients, link) {
		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, m) || c->isfloating || client_is_nonvirtual_fullscreen(c))
			continue;
		count++;
	}

	if (count == 0) {
		tabhdr_disable(vout);
		return;
	}

	tabhdrclear(vout);
	wlr_scene_node_set_enabled(&tree->node, 1);
	active_is_virtual_fs = active && client_is_virtual_fullscreen(active);
	if (active_is_virtual_fs)
		wlr_scene_node_raise_to_top(&tree->node);

	if (style->position == TABHDR_TOP)
		wlr_scene_node_set_position(&tree->node, area.x, area.y);
	else
		wlr_scene_node_set_position(&tree->node, area.x, area.y + area.height - height);

	base_width = area.width / count;
	remainder = area.width % count;
	if (m && m->wlr_output && m->wlr_output->scale > 0.0f)
		scale = m->wlr_output->scale;

	wl_list_for_each(c, &clients, link) {
		const float *bgcolor;
		const float *fgcolor;
		struct wlr_scene_tree *tabtree;
		struct wlr_scene_rect *rect;
		const char *title;
		char *transformed;
		const char *render_title;
		int w;

		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, m) || c->isfloating || client_is_nonvirtual_fullscreen(c))
			continue;
		w = base_width + (idx < remainder ? 1 : 0);
		tabtree = wlr_scene_tree_create(tree);
		bgcolor = (c == active) ? style->active_color : style->inactive_color;
		fgcolor = (c == active) ? style->text_active_color : style->text_inactive_color;
		title = client_get_title(c);
		transformed = tabhdr_transform_title(title);
		render_title = transformed ? transformed : title;
		if (!tabtree) {
			rect = wlr_scene_rect_create(tree, w, height, bgcolor);
			if (rect)
				wlr_scene_node_set_position(&rect->node, x, 0);
		} else {
			wlr_scene_node_set_position(&tabtree->node, x, 0);
			rect = wlr_scene_rect_create(tabtree, w, height, bgcolor);
			if (rect)
				wlr_scene_node_set_position(&rect->node, 0, 0);
			tabhdr_create_text_node(tabtree, render_title, w, height, scale, fgcolor);
			free(transformed);
			transformed = NULL;
		}
		if (transformed)
			free(transformed);
		x += w;
		idx++;
	}
}
