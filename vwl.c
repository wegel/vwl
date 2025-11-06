/*
 * See LICENSE file for copyright and license details.
 */
#include <cairo.h>
#include <getopt.h>
#include <glib-object.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdbool.h>
#include <pango/pangocairo.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <drm_fourcc.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#ifdef MAX
#undef MAX
#endif
#ifdef MIN
#undef MIN
#endif
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

#include "vwl.h"
#include "util.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "vwl-ipc-unstable-v1-protocol.h"
#pragma GCC diagnostic pop

#if DEFAULT_WORKSPACE_ID >= WORKSPACE_COUNT
#error "DEFAULT_WORKSPACE_ID must be less than WORKSPACE_COUNT"
#endif

/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
void arrange(Monitor *m);
void axisnotify(struct wl_listener *listener, void *data);
void buttonpress(struct wl_listener *listener, void *data);
void chvt(const Arg *arg);
void checkidleinhibitor(struct wlr_surface *exclude);
void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
void cleanuplisteners(void);
static void closemon(Monitor *m);
static void commitnotify(struct wl_listener *listener, void *data);
void commitpopup(struct wl_listener *listener, void *data);
void createdecoration(struct wl_listener *listener, void *data);
void createidleinhibitor(struct wl_listener *listener, void *data);
void createkeyboard(struct wlr_keyboard *keyboard);
KeyboardGroup *createkeyboardgroup(void);
void createlayersurface(struct wl_listener *listener, void *data);
void createmon(struct wl_listener *listener, void *data);
void createnotify(struct wl_listener *listener, void *data);
void createpointer(struct wlr_pointer *pointer);
void createpointerconstraint(struct wl_listener *listener, void *data);
void createpopup(struct wl_listener *listener, void *data);
void cursorframe(struct wl_listener *listener, void *data);
void destroydragicon(struct wl_listener *listener, void *data);
void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
void destroypointerconstraint(struct wl_listener *listener, void *data);
void destroykeyboardgroup(struct wl_listener *listener, void *data);
static Monitor *dirtomon(enum wlr_direction dir);
static VirtualOutput *voutneighbor(VirtualOutput *from, enum wlr_direction dir);
void focusclient(Client *c, int lift);
void focusmon(const Arg *arg);
void focusstack(const Arg *arg);
Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
void gpureset(struct wl_listener *listener, void *data);
void handlesig(int signo);
void inputdevice(struct wl_listener *listener, void *data);
int keybinding(uint32_t mods, xkb_keysym_t sym);
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);
void killclient(const Arg *arg);
void locksession(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
void motionabsolute(struct wl_listener *listener, void *data);
void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
void motionrelative(struct wl_listener *listener, void *data);
void moveresize(const Arg *arg);
void outputmgrapply(struct wl_listener *listener, void *data);
void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
void outputmgrtest(struct wl_listener *listener, void *data);
void configurephys(Monitor *m, const MonitorRule *match);
void updatephys(Monitor *m);
void powermgrsetmode(struct wl_listener *listener, void *data);
void quit(const Arg *arg);
void requeststartdrag(struct wl_listener *listener, void *data);
void requestmonstate(struct wl_listener *listener, void *data);
static void resize(Client *c, struct wlr_box geo, int interact);
static void run(char *startup_cmd);
void setcursor(struct wl_listener *listener, void *data);
void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void setworkspace(Client *c, Workspace *ws);
void setpsel(struct wl_listener *listener, void *data);
void setsel(struct wl_listener *listener, void *data);
static void setup(void);
void spawn(const Arg *arg);
void startdrag(struct wl_listener *listener, void *data);
void tag(const Arg *arg);
void tagmon(const Arg *arg);
void tabbed(Monitor *m);
void tabmove(const Arg *arg);
void tile(Monitor *m);
void togglefloating(const Arg *arg);
void togglefullscreen(const Arg *arg);
void toggletabbed(const Arg *arg);
void moveworkspace(const Arg *arg);
static void tabhdrupdate(Monitor *m, VirtualOutput *vout, struct wlr_box area, Client *active);
static void tabhdrdisable(VirtualOutput *vout);
static struct wlr_scene_buffer *tabhdr_create_text_node(struct wlr_scene_tree *parent,
		const char *title, int width, int height, float scale,
		const float color[static 4]);
static bool tabhdr_ignore_input(struct wlr_scene_buffer *buffer, double *sx, double *sy);
static void unmapnotify(struct wl_listener *listener, void *data);
void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
void urgent(struct wl_listener *listener, void *data);
void view(const Arg *arg);
void virtualkeyboard(struct wl_listener *listener, void *data);
void virtualpointer(struct wl_listener *listener, void *data);
Monitor *xytomon(double x, double y);
void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
void zoom(const Arg *arg);
static Workspace *wsbyid(unsigned int id);
static Workspace *wsfindfree(void);
static VirtualOutput *createvout(Monitor *m, const char *name);
static void destroyvout(VirtualOutput *vout);
VirtualOutput *focusvout(Monitor *m);
static void wsactivate(VirtualOutput *vout, Workspace *ws, int focus_change);
static void wsattach(VirtualOutput *vout, Workspace *ws);
static Workspace *wsfirst(VirtualOutput *vout);
static VirtualOutput *firstvout(Monitor *m);
static VirtualOutput *findvoutbyname(Monitor *m, const char *name);
static void wsmoveto(Workspace *ws, VirtualOutput *vout);
VirtualOutput *voutat(Monitor *m, double lx, double ly);
void arrangevout(Monitor *m, const struct wlr_box *usable_area);
static Workspace *wsnext(VirtualOutput *vout, Workspace *exclude);
static void wssave(VirtualOutput *vout);
static void wsload(VirtualOutput *vout, Workspace *ws);
static Client *focustopvout(VirtualOutput *vout);
static void cursorwarptovout(VirtualOutput *vout);
/* removed unused voname function */

/* variables */
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */

static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
extern struct wlr_idle_notifier_v1 *idle_notifier;
extern struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;

static struct wlr_scene_rect *root_bg;
static struct wlr_session_lock_manager_v1 *session_lock_mgr;

static struct wlr_box sgeom;
static bool vt_recovery_mode = false; /* Track if we're recovering from VT switch */

/* Global event handlers are now in plumbing.c */
extern struct wl_listener cursor_axis;
extern struct wl_listener cursor_button;
extern struct wl_listener cursor_frame;
extern struct wl_listener cursor_motion;
extern struct wl_listener cursor_motion_absolute;
extern struct wl_listener gpu_reset;
extern struct wl_listener layout_change;
extern struct wl_listener new_idle_inhibitor;
extern struct wl_listener new_input_device;
extern struct wl_listener new_virtual_keyboard;
extern struct wl_listener new_virtual_pointer;
extern struct wl_listener new_pointer_constraint;
extern struct wl_listener new_output;
extern struct wl_listener new_xdg_toplevel;
extern struct wl_listener new_xdg_popup;
extern struct wl_listener new_xdg_decoration;
extern struct wl_listener new_layer_surface;
extern struct wl_listener output_mgr_apply;
extern struct wl_listener output_mgr_test;
extern struct wl_listener output_power_mgr_set_mode;
extern struct wl_listener request_activate;
extern struct wl_listener request_cursor;
extern struct wl_listener request_set_psel;
extern struct wl_listener request_set_sel;
extern struct wl_listener request_set_cursor_shape;
extern struct wl_listener request_start_drag;
extern struct wl_listener start_drag;
extern struct wl_listener new_session_lock;

#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void associatex11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void createnotifyx11(struct wl_listener *listener, void *data);
static void dissociatex11(struct wl_listener *listener, void *data);
static void sethints(struct wl_listener *listener, void *data);
static void xwaylandready(struct wl_listener *listener, void *data);
extern struct wl_listener new_xwayland_surface;
extern struct wl_listener xwayland_ready;
extern struct wlr_xwayland *xwayland;
#endif

/* configuration, allows nested code to access above variables */
#include "config.h"

/* client.h is already included in vwl.h */

/* function implementations */
void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	/* rule matching */
	const char *appid, *title;
	unsigned int workspace_id = WORKSPACE_COUNT;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;
	Workspace *ws = NULL;

	appid = client_get_appid(c);
	title = client_get_title(c);

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			if (r->workspace < WORKSPACE_COUNT)
				workspace_id = r->workspace;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	c->isfloating |= client_is_float_type(c);
	if (!mon)
		mon = selmon;
	if (workspace_id < WORKSPACE_COUNT)
		ws = wsbyid(workspace_id);
	if (!ws && mon)
		ws = MON_FOCUS_WS(mon);
	if (ws && mon && ws->vout && ws->vout->mon != mon) {
		VirtualOutput *target_vout = focusvout(mon);
		if (target_vout)
			wsmoveto(ws, target_vout);
	}
	if (!ws && selmon)
		ws = MON_FOCUS_WS(selmon);
	if (!ws)
		ws = selws;
	if (!ws)
		ws = wsbyid(DEFAULT_WORKSPACE_ID);
	setworkspace(c, ws);
}

struct TabTextBuffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
};

struct TabFontMetrics {
	int logical_height;
};

static const struct TabFontMetrics *tabhdr_font_metrics(void);
static int tabhdr_header_height(void);
static char *tabhdr_transform_title(const char *title);

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
tabhdr_text_buffer_begin(struct wlr_buffer *buffer, uint32_t flags, void **data,
		uint32_t *format, size_t *stride)
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

	if (!initialized) {
		cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cairo_t *cr = cairo_create(surface);
		PangoLayout *layout = NULL;
		PangoFontDescription *font = NULL;
		PangoRectangle logical = {0};

		if (cr)
			layout = pango_cairo_create_layout(cr);
		if (layout)
			font = pango_font_description_from_string(tabhdr_font);

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

static int
tabhdr_header_height(void)
{
	const struct TabFontMetrics *fm = tabhdr_font_metrics();
	int content = fm->logical_height;
	if (content < 0)
		content = 0;
	return tabhdr_padding_top + content + tabhdr_padding_bottom;
}

static char *
tabhdr_transform_title(const char *title)
{
	const TabTitleTransformRule *rule;
	char *current;
	bool changed = false;

	if (!title)
		title = "";
	if (!tabhdr_title_transforms[0].pattern)
		return NULL;

	current = strdup(title);
	if (!current)
		return NULL;

	for (rule = tabhdr_title_transforms; rule->pattern; rule++) {
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
tabhdr_create_text_node(struct wlr_scene_tree *parent, const char *title,
		int width, int height, float scale, const float color[static 4])
{
	cairo_surface_t *surface = NULL;
	cairo_t *cr = NULL;
	PangoLayout *layout = NULL;
	PangoFontDescription *font = NULL;
	struct TabTextBuffer *buffer = NULL;
	struct wlr_scene_buffer *node = NULL;
	int text_width, surf_w, surf_h, text_y, max_y;
	int top_pad = tabhdr_padding_top;
	int bottom_pad = tabhdr_padding_bottom;
	int left_pad = tabhdr_padding_left;
	int right_pad = tabhdr_padding_right;
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
	font = pango_font_description_from_string(tabhdr_font);
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
	wl_list_for_each_safe(node, tmp, &vout->tabhdr->children, link)
		wlr_scene_node_destroy(node);
}

static void
tabhdrdisable(VirtualOutput *vout)
{
	if (!vout || !vout->tabhdr)
		return;
	tabhdrclear(vout);
	wlr_scene_node_set_enabled(&vout->tabhdr->node, 0);
}

static void
tabhdrupdate(Monitor *m, VirtualOutput *vout, struct wlr_box area, Client *active)
{
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
		tabhdrdisable(vout);
		return;
	}

	wl_list_for_each(c, &clients, link) {
		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, m) || c->isfloating ||
		    client_is_nonvirtual_fullscreen(c))
			continue;
		count++;
	}

	if (count == 0) {
		tabhdrdisable(vout);
		return;
	}

	tabhdrclear(vout);
	wlr_scene_node_set_enabled(&tree->node, 1);
	active_is_virtual_fs = active && client_is_virtual_fullscreen(active);
	if (active_is_virtual_fs)
		wlr_scene_node_raise_to_top(&tree->node);

	if (tabhdr_position == TABHDR_TOP)
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

		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, m) || c->isfloating ||
		    client_is_nonvirtual_fullscreen(c))
			continue;
		w = base_width + (idx < remainder ? 1 : 0);
		tabtree = wlr_scene_tree_create(tree);
		bgcolor = (c == active) ? tabhdr_active_color : tabhdr_inactive_color;
		fgcolor = (c == active) ? tabhdr_text_active_color : tabhdr_text_inactive_color;
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

void
arrange(Monitor *m)
{
	Client *c;
	VirtualOutput *vout;
	VirtualOutput *prev_focus = focusvout(m);

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	c = focustop(m);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			c && c->isfullscreen && c->fullscreen_mode == FS_MONITOR);

	wl_list_for_each(vout, &m->vouts, link) {
		m->focus_vout = vout;
		strncpy(vout->ltsymbol, vout->lt[vout->sellt]->symbol, LENGTH(vout->ltsymbol));
		vout->ltsymbol[LENGTH(vout->ltsymbol) - 1] = '\0';

		/* We move all clients (except fullscreen and unmanaged) to LyrTile while
		 * in floating layout to avoid "real" floating clients be always on top */
		wl_list_for_each(c, &clients, link) {
			struct wlr_scene_tree *parent = c->scene->node.parent;

			if (CLIENT_VOUT(c) != vout || c->mon != m || c->scene->node.parent == layers[LyrFS])
				continue;

			if (c->isfloating) {
				if (!vout->lt[vout->sellt]->arrange)
					parent = layers[LyrTile];
				else
					parent = layers[LyrFloat];
			}

			if (parent != c->scene->node.parent)
				wlr_scene_node_reparent(&c->scene->node, parent);
		}

		if (vout->lt[vout->sellt]->arrange) {
			vout->lt[vout->sellt]->arrange(m);
			if (vout->lt[vout->sellt]->arrange != tabbed)
				tabhdrdisable(vout);
		} else {
			tabhdrdisable(vout);
		}
	}
	m->focus_vout = prev_focus ? prev_focus : firstvout(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	IPCOutput *ipc_output, *ipc_output_tmp;
	size_t i;

	wl_list_for_each_safe(ipc_output, ipc_output_tmp, &m->ipc_outputs, link)
		wl_resource_destroy(ipc_output->resource);

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		destroylocksurface(&m->destroy_lock_surface, NULL);
	m->wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	free(m);
}

void
closemon(Monitor *m)
{
	/* update selmon if needed and
	 * move closed monitor's clients to the focused one */
	Client *c;
	Workspace *ws, *wtmp;
	VirtualOutput *vout, *vtmp;
	Monitor *target;
	VirtualOutput *target_vout = NULL;
	int i = 0, nmons = wl_list_length(&mons);
	vt_recovery_mode = true;
	if (!nmons) {
		selmon = NULL;
	} else if (m == selmon) {
		do /* don't switch to disabled mons */
			selmon = wl_container_of(mons.next, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

		if (!selmon->wlr_output->enabled)
			selmon = NULL;
	}

	target = selmon;
	selvout = target ? focusvout(target) : NULL;
	if (target)
		target_vout = focusvout(target);
	wl_list_for_each_safe(vout, vtmp, &m->vouts, link) {
		wl_list_for_each_safe(ws, wtmp, &vout->workspaces, link) {
			if (target_vout) {
				if (!ws->orphan_vout_name[0])
					snprintf(ws->orphan_vout_name, sizeof(ws->orphan_vout_name), "%s", vout->name);
				if (!ws->orphan_monitor_name[0] && m && m->wlr_output)
					snprintf(ws->orphan_monitor_name, sizeof(ws->orphan_monitor_name), "%s", m->wlr_output->name);
				ws->was_orphaned = true;
				wsmoveto(ws, target_vout);
				/* target_vout may gain focus workspace; keep pointer current */
				target_vout = focusvout(target);
			} else {
				wssave(vout);
				wl_list_remove(&ws->link);
				wl_list_init(&ws->link);
				ws->vout = NULL;
				ws->was_orphaned = true;
				if (!ws->orphan_vout_name[0])
					snprintf(ws->orphan_vout_name, sizeof(ws->orphan_vout_name), "%s", vout->name);
				if (!ws->orphan_monitor_name[0] && m && m->wlr_output)
					snprintf(ws->orphan_monitor_name, sizeof(ws->orphan_monitor_name), "%s", m->wlr_output->name);
			}
		}
		destroyvout(vout);
	}
	if (target && target_vout && !target_vout->ws)
		wsactivate(target_vout, wsfirst(target_vout), 0);

	wl_list_for_each(c, &clients, link) {
		if (c->isfloating && c->geom.x > m->monitor_area.width)
			resize(c, (struct wlr_box){.x = c->geom.x - m->window_area.width, .y = c->geom.y,
					.width = c->geom.width, .height = c->geom.height}, 0);
		if (c->mon == m)
			setworkspace(c, c->ws);
	}
	focusclient(focustop(selmon), 1);
	updateipc();
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		/*
		 * Get the monitor this client will be rendered on
		 * Note that if the user set a rule in which the client is placed on
		 * a different monitor based on its title, this will likely select
		 * a wrong monitor.
		 */
		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setworkspace(c, NULL); /* Make sure to reapply rules in mapnotify() */

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	const MonitorRule *r, *match = NULL;
	size_t i, matched_count;
	struct wlr_output_state state;
	Monitor *m;
	VirtualOutput *vout;
	VirtualOutput *first_vout;
	const VirtualOutputRule *matched_rules[LENGTH(vorules)];
	int first;
	bool found_orphans = false;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
	wl_list_init(&m->vouts);
	wl_list_init(&m->ipc_outputs);

	for (i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->monitor_area.x = r->x;
			m->monitor_area.y = r->y;
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);
			match = r;
			break;
		}
	}

	configurephys(m, match);

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	wlr_xcursor_manager_load(cursor_mgr, wlr_output->scale);

	first = wl_list_empty(&mons);
	wl_list_insert(&mons, &m->link);
	if (!selmon)
		selmon = m;

	/* The xdg-protocol specifies:
	 *
	 * If the fullscreened surface is not opaque, the compositor must make
	 * sure that other screen content not part of the same surface tree (made
	 * up of subsurfaces, popups or similarly coupled surfaces) are not
	 * visible below the fullscreened surface.
	 *
	 */
	/* updatemons() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	/* Adds this to the output layout in the order it was configured.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->monitor_area.x == -1 && m->monitor_area.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->monitor_area.x, m->monitor_area.y);

	matched_count = 0;
	first_vout = NULL;
	for (i = 0; i < LENGTH(vorules); i++) {
		const VirtualOutputRule *vr = &vorules[i];
		if (!vr->monitor)
			continue;
		if (strstr(wlr_output->name, vr->monitor))
			matched_rules[matched_count++] = vr;
	}
	if (!matched_count) {
		for (i = 0; i < LENGTH(vorules); i++) {
			const VirtualOutputRule *vr = &vorules[i];
			if (!vr->monitor)
				matched_rules[matched_count++] = vr;
		}
	}
	if (!matched_count) {
		matched_rules[matched_count++] = NULL;
	}

	first_vout = NULL;
	for (i = 0; i < matched_count; i++) {
		const VirtualOutputRule *vr = matched_rules[i];
		VirtualOutput *new_vout;
		if (vr) {
			new_vout = createvout(m, vr->name);
			if (vr->mfact > 0)
				new_vout->mfact = vr->mfact;
			if (vr->nmaster > 0)
				new_vout->nmaster = vr->nmaster;
			if (vr->lt_primary)
				new_vout->lt[0] = vr->lt_primary;
			if (vr->lt_secondary)
				new_vout->lt[1] = vr->lt_secondary;
			new_vout->sellt = 0;
			new_vout->rule_geom = (struct wlr_box){
				.x = vr->x,
				.y = vr->y,
				.width = vr->width,
				.height = vr->height,
			};
			strncpy(new_vout->ltsymbol, new_vout->lt[new_vout->sellt]->symbol, LENGTH(new_vout->ltsymbol));
			new_vout->ltsymbol[LENGTH(new_vout->ltsymbol) - 1] = '\0';
		} else {
			new_vout = createvout(m, NULL);
		}
		if (!first_vout)
			first_vout = new_vout;
	}
	if (!first_vout)
		first_vout = firstvout(m);

	if (first) {
		Workspace *default_ws = &workspaces[DEFAULT_WORKSPACE_ID];
		if (!vt_recovery_mode) {
			if (first_vout && !default_ws->vout)
				wsattach(first_vout, default_ws);
			if (first_vout)
				wsactivate(first_vout, default_ws, 0);
			selws = default_ws;
		} else {
			selws = NULL;
		}
		selvout = first_vout;
	} else if (!vt_recovery_mode) {
		wl_list_for_each(vout, &m->vouts, link) {
			Workspace *first_ws = wsfirst(vout);
			if (!first_ws) {
				Workspace *unassigned = wsfindfree();
				if (unassigned)
					wsattach(vout, unassigned);
				first_ws = wsfirst(vout);
			}
			if (first_ws)
				wsactivate(vout, first_ws, 0);
		}
	}

	/* Reattach any orphaned workspaces to this monitor */
	for (i = 0; i < WORKSPACE_COUNT; i++) {
		Workspace *ws = &workspaces[i];
		VirtualOutput *target_vout;
		Client *client;
		/* Only reattach workspaces that were explicitly orphaned */
		if (!ws->was_orphaned)
			continue;
		if (ws->orphan_monitor_name[0] && strcmp(ws->orphan_monitor_name, m->wlr_output->name))
			continue;
		found_orphans = true;
		target_vout = findvoutbyname(m, ws->orphan_vout_name);
		if (!target_vout)
			target_vout = first_vout;
		if (!target_vout)
			continue;
		wsattach(target_vout, ws);
		wsload(target_vout, ws);
		ws->was_orphaned = false; /* Clear the flag after reattachment */
		ws->orphan_vout_name[0] = '\0';
		ws->orphan_monitor_name[0] = '\0';
		/* Update monitor pointers for all clients on this workspace */
		wl_list_for_each(client, &clients, link) {
			if (client->ws == ws)
				client->mon = target_vout->mon;
		}
	}

	if (found_orphans) {
		vt_recovery_mode = true;
	}

	if (selmon == m) {
		selvout = focusvout(m);
		selws = selvout ? selvout->ws : NULL;
	}
	arrange(m);
	updateipc();
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client creates a new toplevel (application window). */
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	/* Allocate a Client for this surface */
	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = borderpx;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		wl_list_remove(&c->activate.link);
		wl_list_remove(&c->associate.link);
		wl_list_remove(&c->configure.link);
		wl_list_remove(&c->dissociate.link);
		wl_list_remove(&c->set_hints.link);
	} else
#endif
	{
		wl_list_remove(&c->commit.link);
		wl_list_remove(&c->map.link);
		wl_list_remove(&c->unmap.link);
		wl_list_remove(&c->maximize.link);
	}
	free(c);
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
		return selmon;
	if ((next = wlr_output_layout_adjacent_output(output_layout,
			dir, selmon->wlr_output, selmon->monitor_area.x, selmon->monitor_area.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout,
			dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
			selmon->wlr_output, selmon->monitor_area.x, selmon->monitor_area.y)))
		return next->data;
	return selmon;
}

static VirtualOutput *
voutneighbor(VirtualOutput *from, enum wlr_direction dir)
{
	static const enum wlr_direction order[] = {
		WLR_DIRECTION_LEFT,
		WLR_DIRECTION_RIGHT,
		WLR_DIRECTION_UP,
		WLR_DIRECTION_DOWN,
	};
	struct wlr_box ref;
	double ref_cx, ref_cy;
	size_t i;

	if (!from || !from->mon)
		return NULL;
	ref = (from->layout_geom.width > 0 && from->layout_geom.height > 0)
			? from->layout_geom : from->mon->window_area;
	if (ref.width <= 0 || ref.height <= 0)
		return NULL;
	ref_cx = ref.x + ref.width / 2.0;
	ref_cy = ref.y + ref.height / 2.0;

	for (i = 0; i < LENGTH(order); i++) {
		enum wlr_direction want = order[i];
		VirtualOutput *best = NULL, *vout;
		double best_metric = (want == WLR_DIRECTION_LEFT || want == WLR_DIRECTION_UP)
				? -DBL_MAX : DBL_MAX;

		if (dir && !(dir & want))
			continue;
		wl_list_for_each(vout, &from->mon->vouts, link) {
			struct wlr_box box;
			double cx, cy;
			bool overlaps;

			if (vout == from)
				continue;
			box = (vout->layout_geom.width > 0 && vout->layout_geom.height > 0)
					? vout->layout_geom : vout->mon->window_area;
			if (box.width <= 0 || box.height <= 0)
				continue;
			cx = box.x + box.width / 2.0;
			cy = box.y + box.height / 2.0;
			if (want == WLR_DIRECTION_LEFT || want == WLR_DIRECTION_RIGHT)
				overlaps = box.y < ref.y + ref.height && ref.y < box.y + box.height;
			else
				overlaps = box.x < ref.x + ref.width && ref.x < box.x + box.width;
			if (!overlaps)
				continue;
			switch (want) {
			case WLR_DIRECTION_LEFT:
				if (cx >= ref_cx)
					continue;
				if (cx > best_metric)
					best_metric = cx, best = vout;
				break;
			case WLR_DIRECTION_RIGHT:
				if (cx <= ref_cx)
					continue;
				if (cx < best_metric)
					best_metric = cx, best = vout;
				break;
			case WLR_DIRECTION_UP:
				if (cy >= ref_cy)
					continue;
				if (cy > best_metric)
					best_metric = cy, best = vout;
				break;
			case WLR_DIRECTION_DOWN:
				if (cy <= ref_cy)
					continue;
				if (cy < best_metric)
					best_metric = cy, best = vout;
				break;
			default:
				break;
			}
		}
		if (best)
			return best;
	}
	return NULL;
}

static void
cursorwarptovout(VirtualOutput *vout)
{
	struct wlr_box area;
	double cx, cy;

	if (!cursor || !vout || !vout->mon)
		return;
	area = (vout->layout_geom.width > 0 && vout->layout_geom.height > 0)
			? vout->layout_geom : vout->mon->window_area;
	if (area.width <= 0 || area.height <= 0)
		return;
	cx = area.x + area.width / 2.0;
	cy = area.y + area.height / 2.0;
	wlr_cursor_warp(cursor, NULL, cx, cy);
	cursorsync();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Monitor *tabbed_mon = NULL;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (locked)
		return;

	/* Raise client in stacking order if requested */
	if (c && lift) {
		VirtualOutput *vout = CLIENT_VOUT(c);
		wlr_scene_node_raise_to_top(&c->scene->node);
		if (vout && vout->tabhdr && vout->lt[vout->sellt] &&
			vout->lt[vout->sellt]->arrange == tabbed &&
			client_is_virtual_fullscreen(c))
			wlr_scene_node_raise_to_top(&vout->tabhdr->node);
	}

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	/* Put the new client atop the focus stack and select its monitor */
	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		selvout = CLIENT_VOUT(c);
		if (selvout && selvout->mon != selmon)
			selvout = focusvout(selmon);
		if (selvout && selvout->mon == selmon)
			selws = selvout->ws;
		if (selmon && selvout)
			selmon->focus_vout = selvout;
		if (selvout && selvout->lt[selvout->sellt]
				&& selvout->lt[selvout->sellt]->arrange == tabbed) {
			struct wlr_box area;
			tabbed_mon = selvout->mon;
			area = (selvout->layout_geom.width && selvout->layout_geom.height)
				? selvout->layout_geom : selvout->mon->window_area;
			tabhdrupdate(selvout->mon, selvout, area, c);
		}
		c->isurgent = 0;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag)
			client_set_border_color(c, focuscolor);
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with focuscolor
		 * and focus it after the overlay is closed. */
		if (old_client_type == LayerShell && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		/* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
		 * and probably other clients */
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			client_set_border_color(old_c, bordercolor);

			client_activate_surface(old, 0);
		}
	}
	if (tabbed_mon)
		arrange(tabbed_mon);

	updateipc();

	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Change cursor surface */
	motionnotify(0, NULL, 0, 0, 0, 0);

	/* Have a client, so focus its top-level wlr_surface */
	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

	/* Activate the new client */
	client_activate_surface(client_surface(c), 1);
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do /* don't switch to disabled mons */
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	if (selmon) {
		selvout = focusvout(selmon);
		selws = selvout ? selvout->ws : NULL;
	}
	focusclient(focustop(selmon), 1);
}

static bool
tiling_locked_by_fullscreen(Client *sel)
{
	VirtualOutput *vout;
	const Layout *layout;

	if (!sel || client_has_children(sel) || !sel->isfullscreen)
		return false;
	if (client_is_nonvirtual_fullscreen(sel))
		return true;
	vout = CLIENT_VOUT(sel);
	if (!vout || !vout->lt[vout->sellt])
		return true;
	layout = vout->lt[vout->sellt];
	if (!layout || layout->arrange == tabbed)
		return false;
	return true;
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = focustop(selmon);
	VirtualOutput *vout = focusvout(selmon);
	bool restrict_to_vout = false;
	if (!sel || tiling_locked_by_fullscreen(sel))
		return;
	if (vout && vout->lt[vout->sellt] && vout->lt[vout->sellt]->arrange == tabbed)
		restrict_to_vout = true;
	c = sel;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (restrict_to_vout && CLIENT_VOUT(c) != vout)
				continue;
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (restrict_to_vout && CLIENT_VOUT(c) != vout)
				continue;
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
}

void
tabmove(const Arg *arg)
{
	Client *sel = focustop(selmon);
	VirtualOutput *vout = focusvout(selmon);
	struct wl_list *link;
	Client *target = NULL;
	int dir;

	if (!sel || !vout || !arg)
		return;
	if (!vout->lt[vout->sellt] || vout->lt[vout->sellt]->arrange != tabbed)
		return;
	if (CLIENT_VOUT(sel) != vout || sel->isfloating || client_is_nonvirtual_fullscreen(sel))
		return;

	dir = arg->i;
	if (dir == 0)
		return;

	if (dir > 0) {
		for (link = sel->link.next; link != &clients; link = link->next) {
			Client *c = wl_container_of(link, sel, link);
			if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, selmon)
					|| c->isfloating || client_is_nonvirtual_fullscreen(c))
				continue;
			target = c;
			break;
		}
		if (!target)
			return;
		wl_list_remove(&sel->link);
		wl_list_insert(&target->link, &sel->link);
	} else {
		for (link = sel->link.prev; link != &clients; link = link->prev) {
			Client *c = wl_container_of(link, sel, link);
			if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, selmon)
					|| c->isfloating || client_is_nonvirtual_fullscreen(c))
				continue;
			target = c;
			break;
		}
		if (!target)
			return;
		wl_list_remove(&sel->link);
		wl_list_insert(target->link.prev, &sel->link);
	}

	{
		struct wlr_box area;
		area = (vout->layout_geom.width && vout->layout_geom.height)
			? vout->layout_geom : selmon->window_area;
		tabhdrupdate(selmon, vout, area, sel);
	}
	focusclient(sel, 1);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client *
focustop(Monitor *m)
{
	return focustopvout(focusvout(m));
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	if (client_wants_fullscreen(c)) {
		c->fullscreen_mode = FS_VIRTUAL;
		setfullscreen(c, 1);
	} else {
		setfullscreen(c, 0);
	}
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;
	int i;

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	/* Enabled later by a call to arrange() */
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	/* Handle unmanaged clients first so we can return prior create borders */
	if (client_is_unmanaged(c)) {
		/* Unmanaged clients always are floating */
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0,
				c->isurgent ? urgentcolor : bordercolor);
		c->border[i]->node.data = c;
	}

	/* Initialize client geometry with room for border */
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	/* Insert this client into client lists. */
	wl_list_insert(clients.prev, &c->link);
	wl_list_insert(&fstack, &c->flink);

	/* Set initial workspace, floating status, and focus:
	 * we always consider floating, clients that have parent and thus
	 * we set the same workspace and monitor as its parent.
	 * If there is no parent, apply rules */
	if ((p = client_get_parent(c))) {
		Workspace *target_ws = p->ws;
		c->isfloating = 1;
		if (!target_ws && p->mon)
			target_ws = MON_FOCUS_WS(p->mon);
		setworkspace(c, target_ws);
	} else {
		applyrules(c);
	}
	if (vt_recovery_mode && c->ws)
		vt_recovery_mode = false;
	updateipc();

unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w == c || w == p)
			continue;
		if (!w->isfullscreen || w->mon != m || w->ws != c->ws)
			continue;
		if (client_is_virtual_fullscreen(w))
			continue;
		setfullscreen(w, 0);
	}
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. dwl doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * Since xdg-shell protocol v5 we should ignore request of unsupported
	 * capabilities, just schedule a empty configure when the client uses <5
	 * protocol version
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;
	Monitor *hover_mon;
	VirtualOutput *hover_vout;
	Monitor *prev_mon = cursor_phys.mon ? cursor_phys.mon : cursor_phys.last_mon;
	double prev_mm_x = cursor_phys.x_mm;
	double prev_mm_y = cursor_phys.y_mm;

	if (!prev_mon && cursor) {
		cursorsync();
		prev_mon = cursor_phys.mon ? cursor_phys.mon : cursor_phys.last_mon;
		prev_mm_x = cursor_phys.x_mm;
		prev_mm_y = cursor_phys.y_mm;
	}

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);
		cursorinteg(dx, dy);

		if (enable_physical_cursor_gap_jumps
				&& prev_mon
				&& cursor_mode == CurNormal
				&& !seat->drag
				&& (!active_constraint || active_constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED)
				&& cursorgap(prev_mon, prev_mm_x, prev_mm_y)) {
			return;
		}

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		hover_mon = xytomon(cursor->x, cursor->y);
		hover_vout = NULL;
		if (hover_mon) {
			hover_vout = voutat(hover_mon, cursor->x, cursor->y);
			if (hover_vout)
				hover_mon->focus_vout = hover_vout;
		}

			cursorsync();

		/* Update selmon (even while dragging a window) */
		if (sloppyfocus) {
			selmon = hover_mon;
			if (selmon) {
				selvout = hover_vout ? hover_vout : focusvout(selmon);
			}
		}
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position. */
		resize(grabc, (struct wlr_box){.x = (int)round(cursor->x) - grabcx, .y = (int)round(cursor->y) - grabcy,
			.width = grabc->geom.width, .height = grabc->geom.height}, 1);
		cursorsync();
		return;
	} else if (cursor_mode == CurResize) {
		resize(grabc, (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
			.width = (int)round(cursor->x) - grabc->geom.x, .height = (int)round(cursor->y) - grabc->geom.y}, 1);
		cursorsync();
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	pointerfocus(c, surface, sx, sy, time);
	cursorsync();
}

void
moveresize(const Arg *arg)
{
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	/* Float the window and tell motionnotify to grab it */
	setfloating(grabc, 1);
	switch (cursor_mode = arg->ui) {
	case CurMove:
		grabcx = (int)round(cursor->x) - grabc->geom.x;
		grabcy = (int)round(cursor->y) - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize:
		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started */
		wlr_cursor_warp_closest(cursor, NULL,
				grabc->geom.x + grabc->geom.width,
				grabc->geom.y + grabc->geom.height);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		cursorsync();
		break;
	}
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box limit;
	struct wlr_box *bbox;
	VirtualOutput *vout = CLIENT_VOUT(c);
	struct wlr_box clip;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	limit = (vout && vout->layout_geom.width && vout->layout_geom.height) ? vout->layout_geom : c->mon->window_area;
	bbox = interact ? &sgeom : &limit;

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	applybounds(c, bbox);

	/* Update scene-graph, including borders */
	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
	wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
	wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

	/* this is a no-op if size hasn't changed */
	c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
			c->geom.height - 2 * c->bw);
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

void
run(char *startup_cmd)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	/* Now that the socket exists and the backend is started, run the startup command */
	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0)
			die("startup: pipe:");
		if ((child_pid = fork()) < 0)
			die("startup: fork:");
		if (child_pid == 0) {
			setsid();
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			die("startup: execl:");
		}
		dup2(piperw[1], STDOUT_FILENO);
		close(piperw[1]);
		close(piperw[0]);
	}

	updateipc();

	/* At this point the outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* TODO hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping. Still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	cursorsync();

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wl_display_run(dpy);
}

void
setfloating(Client *c, int floating)
{
	Client *p = client_get_parent(c);
	VirtualOutput *vout = CLIENT_VOUT(c);
	c->isfloating = floating;
	/* If in floating layout do not change the client's layer */
	if (!vout || !client_surface(c)->mapped || !vout->lt[vout->sellt]->arrange)
		return;
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
			(p && p->isfullscreen) ? LyrFS
			: c->isfloating ? LyrFloat : LyrTile]);
	arrange(c->mon);
	updateipc();
}

void
setfullscreen(Client *c, int fullscreen)
{
	VirtualOutput *vout;
	struct wlr_box target;
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : borderpx;
	client_set_fullscreen(c, fullscreen);

	if (fullscreen) {
		c->prev = c->geom;
		vout = CLIENT_VOUT(c);
		if (c->fullscreen_mode == FS_NONE)
			c->fullscreen_mode = FS_VIRTUAL;
		if (c->fullscreen_mode == FS_MONITOR || !vout)
			wlr_scene_node_reparent(&c->scene->node, layers[LyrFS]);
		else
			wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		target = c->mon->monitor_area;
		if (c->fullscreen_mode == FS_VIRTUAL && vout && vout->layout_geom.width && vout->layout_geom.height) {
			target = vout->layout_geom;
			if (vout->lt[vout->sellt] && vout->lt[vout->sellt]->arrange == tabbed) {
				int header = tabhdr_header_height();
				if (header > 0 && target.height > header) {
					target.height -= header;
					if (tabhdr_position == TABHDR_TOP)
						target.y += header;
				}
			}
		}
		resize(c, target, 0);
		if (c->fullscreen_mode == FS_VIRTUAL)
			wlr_scene_node_raise_to_top(&c->scene->node);
	} else {
		wlr_scene_node_reparent(&c->scene->node, layers[c->isfloating ? LyrFloat : LyrTile]);
		/* restore previous size instead of arrange for floating windows since
		 * client positions are set by the user and cannot be recalculated */
		c->fullscreen_mode = FS_NONE;
		resize(c, c->prev, 0);
	}
	arrange(c->mon);
	updateipc();
}

void
setlayout(const Arg *arg)
{
	VirtualOutput *vout = focusvout(selmon);

	if (!vout)
		return;
	if (!arg || !arg->v || arg->v != vout->lt[vout->sellt])
		vout->sellt ^= 1;
	if (arg && arg->v)
		vout->lt[vout->sellt] = (Layout *)arg->v;
	strncpy(vout->ltsymbol, vout->lt[vout->sellt]->symbol, LENGTH(vout->ltsymbol));
	vout->ltsymbol[LENGTH(vout->ltsymbol) - 1] = '\0';
	wssave(vout);
	arrange(selmon);
	updateipc();
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;
 	VirtualOutput *vout = focusvout(selmon);

	if (!arg || !vout || !vout->lt[vout->sellt]->arrange)
		return;
	f = arg->f < 1.0f ? arg->f + vout->mfact : arg->f - 1.0f;
	if (f < 0.1 || f > 0.9)
		return;
	vout->mfact = f;
	wssave(vout);
	arrange(selmon);
}

void
setworkspace(Client *c, Workspace *ws)
{
	Monitor *oldmon = c->mon;
	VirtualOutput *vout = ws ? ws->vout : NULL;
	Workspace *oldws = c->ws;
	Monitor *newmon = vout ? vout->mon : NULL;
	int workspace_changed = oldws != ws;
	if (!workspace_changed && oldmon == newmon)
		return;

	c->ws = ws;
	c->mon = newmon;
	c->prev = c->geom;

	if (oldmon && oldmon != c->mon)
		arrange(oldmon);

	if (c->mon) {
		resize(c, c->geom, 0);
		setfullscreen(c, c->isfullscreen);
		setfloating(c, c->isfloating);
	}

	if (selmon && workspace_changed)
		focusclient(focustop(selmon), 1);
}

void
setup(void)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	const VirtualOutputRule *defvorule = NULL;
	size_t j;
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, NULL);

	wlr_log_init(log_level, NULL);

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	if (!(backend = wlr_backend_autocreate(event_loop, &session)))
		die("couldn't create backend");

	/* Initialize the scene graph used to lay out windows */
	scene = wlr_scene_create();
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
	for (i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	for (j = 0; j < LENGTH(vorules); j++) {
		if (!vorules[j].monitor) {
			defvorule = &vorules[j];
			break;
		}
	}
	for (i = 0; i < WORKSPACE_COUNT; i++) {
		Workspace *ws = &workspaces[i];
		ws->id = i;
		snprintf(ws->name, sizeof ws->name, "%u", i);
		ws->vout = NULL;
		wl_list_init(&ws->link);
		ws->state.mfact = defvorule && defvorule->mfact > 0 ? defvorule->mfact : 0.55f;
		ws->state.nmaster = defvorule && defvorule->nmaster > 0 ? defvorule->nmaster : 1;
		ws->state.sellt = 0;
		ws->state.lt[0] = defvorule && defvorule->lt_primary ? defvorule->lt_primary : &layouts[0];
		ws->state.lt[1] = defvorule && defvorule->lt_secondary ? defvorule->lt_secondary :
			(LENGTH(layouts) > 1) ? &layouts[1] : ws->state.lt[0];
		ws->was_orphaned = false;
		ws->orphan_vout_name[0] = '\0';
		ws->orphan_monitor_name[0] = '\0';
	}
	selws = &workspaces[DEFAULT_WORKSPACE_ID];

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	/* Create shm, drm and linux_dmabuf interfaces by ourselves.
	 * The simplest way is to call:
	 *      wlr_renderer_init_wl_display(drw);
	 * but we need to create the linux_dmabuf interface manually to integrate it
	 * with wlr_scene. */
	wlr_renderer_init_wl_shm(drw, dpy);

	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene,
				wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline
			&& backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't create allocator");

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	compositor = wlr_compositor_create(dpy, 6, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_data_control_manager_v1_create(dpy);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_presentation_create(dpy, backend, 2);
	wlr_alpha_modifier_v1_create(dpy);

	/* Initializes the interface used to implement urgency hints */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

	power_mgr = wlr_output_power_manager_v1_create(dpy);
	wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

	/* Creates an output layout, which is a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	wl_signal_add(&backend->events.new_output, &new_output);

	/* Set up our client lists, the xdg-shell and the layer-shell. The xdg-shell is a
	 * Wayland protocol which is used for application windows. For more
	 * detail on shells, refer to the article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	wl_list_init(&clients);
	wl_list_init(&fstack);

	xdg_shell = wlr_xdg_shell_create(dpy, 6);
	wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
	wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(dpy, 3);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

	idle_notifier = wlr_idle_notifier_v1_create(dpy);

	idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
	wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

	session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
	wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
	locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
			(float [4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). Scaled cursors will be loaded with each output. */
	{
		char cursor_size_env[16];
		cursor_mgr = wlr_xcursor_manager_create(NULL, cursor_size);
		snprintf(cursor_size_env, sizeof(cursor_size_env), "%d", cursor_size);
		setenv("XCURSOR_SIZE", cursor_size_env, 1);
	}

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
	wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_signal_add(&backend->events.new_input, &new_input_device);
	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
			&new_virtual_keyboard);
	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
	wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
			&new_virtual_pointer);

	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);

	wl_global_create(dpy, &zvwl_ipc_manager_v1_interface, 1, NULL, ipc_manager_bind);

	/* Make sure XWayland clients don't connect to the parent X server,
	 * e.g when running in the x11 backend or the wayland backend and the
	 * compositor has Xwayland support */
	unsetenv("DISPLAY");
#ifdef XWAYLAND
	/*
	 * Initialise the XWayland X server.
	 * It will be started when the first X client is started.
	 */
	if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
		wl_signal_add(&xwayland->events.ready, &xwayland_ready);
		wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

		setenv("DISPLAY", xwayland->display_name, 1);
	} else {
		fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
	}
#endif
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwl: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void
tag(const Arg *arg)
{
	Client *sel = focustop(selmon);
	Workspace *ws;
	if (!sel)
		return;
	if (arg->ui >= WORKSPACE_COUNT)
		return;
	ws = wsbyid(arg->ui);
	if (!ws)
		return;
	if (!ws->vout && selmon) {
		VirtualOutput *vout = focusvout(selmon);
		if (vout)
			wsattach(vout, ws);
	}
	setworkspace(sel, ws);
	updateipc();
}

void
tagmon(const Arg *arg)
{
	Client *sel = focustop(selmon);
	Monitor *m;
	if (!selmon || !(m = dirtomon(arg->i)))
		return;
	if (sel) {
		VirtualOutput *vout = focusvout(m);
		if (vout && vout->ws)
			setworkspace(sel, vout->ws);
	}
}

void
moveworkspace(const Arg *arg)
{
	Monitor *target = NULL;
	Workspace *active;
	VirtualOutput *origin_vout, *target_vout;
	bool warp_needed;
	Client *focused;

	if (!selmon || !(active = MON_FOCUS_WS(selmon)))
		return;
	origin_vout = active->vout ? active->vout : focusvout(selmon);
	target_vout = voutneighbor(origin_vout, arg->i);
	if (target_vout) {
		target = target_vout->mon;
	} else {
		target = dirtomon(arg->i);
		if (!target || target == selmon)
			return;
		target_vout = focusvout(target);
		if (!target_vout)
			return;
	}

	focused = origin_vout ? focustopvout(origin_vout) : focustop(selmon);
	warp_needed = target_vout != origin_vout;
	wsmoveto(active, target_vout);
	selmon = target;
	selvout = target_vout;
	selws = target_vout->ws;
	if (focused)
		focusclient(focused, 1);
	else
		focusclient(focustop(selmon), 1);
	if (warp_needed)
		cursorwarptovout(target_vout);
	updateipc();
}

void
tabbed(Monitor *m)
{
	VirtualOutput *vout = focusvout(m);
	struct wlr_box area;
	struct wlr_box client_box;
	Client *c, *active;
	int header_height;
	int count = 0;

	if (!vout)
		return;
	area = (vout->layout_geom.width && vout->layout_geom.height) ? vout->layout_geom : m->window_area;
	active = focustopvout(vout);
	header_height = tabhdr_header_height();

	client_box = area;
	if (header_height > 0 && area.height > header_height) {
		client_box.height -= header_height;
		if (tabhdr_position == TABHDR_TOP)
			client_box.y += header_height;
	}

	wl_list_for_each(c, &clients, link) {
		bool virtual_fs;

		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, m))
			continue;
		virtual_fs = client_is_virtual_fullscreen(c);
		if (c->isfloating || (!virtual_fs && c->isfullscreen))
			continue;
		count++;
		if (c == active) {
			if (!virtual_fs && client_box.height > 0 &&
				(c->geom.x != client_box.x || c->geom.y != client_box.y ||
				 c->geom.width != client_box.width || c->geom.height != client_box.height))
				resize(c, client_box, 0);
			wlr_scene_node_set_enabled(&c->scene->node, 1);
			client_set_suspended(c, 0);
		} else {
			wlr_scene_node_set_enabled(&c->scene->node, 0);
			client_set_suspended(c, 1);
		}
	}

	if (count > 1)
		snprintf(vout->ltsymbol, LENGTH(vout->ltsymbol), "[T:%d]", count);

	if (active)
		wlr_scene_node_raise_to_top(&active->scene->node);

	tabhdrupdate(m, vout, area, active);
}

void
tile(Monitor *m)
{
	unsigned int mw, my, ty;
	int i, n = 0;
	Client *c;
	VirtualOutput *vout = focusvout(m);
	struct wlr_box area;

	if (!vout)
		return;
	area = (vout->layout_geom.width && vout->layout_geom.height) ? vout->layout_geom : m->window_area;

	wl_list_for_each(c, &clients, link)
		if (CLIENT_VOUT(c) == vout && VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	if (n > vout->nmaster)
		mw = vout->nmaster ? (int)roundf(area.width * vout->mfact) : 0;
	else
		mw = area.width;
	i = my = ty = 0;
	wl_list_for_each(c, &clients, link) {
		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (i < vout->nmaster) {
			resize(c, (struct wlr_box){.x = area.x, .y = area.y + my, .width = mw,
				.height = (area.height - my) / (MIN(n, vout->nmaster) - i)}, 0);
			my += c->geom.height;
		} else {
			resize(c, (struct wlr_box){.x = area.x + mw, .y = area.y + ty,
				.width = area.width - mw, .height = (area.height - ty) / (n - i)}, 0);
			ty += c->geom.height;
		}
		i++;
	}
}

void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	/* return if fullscreen */
	if (sel && !sel->isfullscreen)
		setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel)
		return;
	if (!sel->isfullscreen) {
		sel->fullscreen_mode = FS_VIRTUAL;
		setfullscreen(sel, 1);
	} else if (sel->fullscreen_mode == FS_VIRTUAL) {
		sel->fullscreen_mode = FS_MONITOR;
		setfullscreen(sel, 1);
	} else {
		setfullscreen(sel, 0);
	}
}

void
toggletabbed(const Arg *arg)
{
	const Layout *tab = arg && arg->v ? arg->v : NULL;
	Arg tile_arg = {.v = &layouts[0]};
	VirtualOutput *vout = focusvout(selmon);
	if (!vout || !tab)
		return;
	if (vout->lt[vout->sellt] == tab)
		setlayout(&tile_arg);
	else
		setlayout(&(Arg){.v = tab});
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(selmon), 1);
		}
	} else {
		wl_list_remove(&c->link);
		/* Preserve workspace during VT recovery or when vout is NULL */
		if (!vt_recovery_mode && c->ws && c->ws->vout)
			setworkspace(c, NULL);
		wl_list_remove(&c->flink);
	}

	wlr_scene_node_destroy(&c->scene->node);
	updateipc();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
updatemons(struct wl_listener *listener, void *data)
{
	/*
	 * Called whenever the output layout changes: adding or removing a
	 * monitor, changing an output's mode or position, etc. This is where
	 * the change officially happens and we update geometry, window
	 * positions, focus, and the stored configuration in wlroots'
	 * output-manager implementation.
	 */
	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;
	struct wlr_box usable;

	/* First remove from the layout the disabled monitors */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled || m->asleep)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		/* Remove this output from the layout to avoid cursor enter inside it */
		wlr_output_layout_remove(output_layout, m->wlr_output);
		closemon(m);
		m->monitor_area = m->window_area = (struct wlr_box){0};
	}
	/* Insert outputs that need to */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	/* Make sure the clients are hidden when dwl is locked */
	wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled) {
			continue;
		}
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		wlr_output_layout_get_box(output_layout, m->wlr_output, &m->monitor_area);
		m->window_area = m->monitor_area;
		wlr_scene_output_set_position(m->scene_output, m->monitor_area.x, m->monitor_area.y);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->monitor_area.x, m->monitor_area.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->monitor_area.width, m->monitor_area.height);

		updatephys(m);

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->monitor_area.x, m->monitor_area.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->monitor_area.width, m->monitor_area.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		usable = m->window_area;
		arrangevout(m, &usable);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);
		/* make sure fullscreen clients have the right size */
		if ((c = focustop(m)) && c->isfullscreen) {
			if (c->fullscreen_mode == FS_MONITOR) {
				resize(c, m->monitor_area, 0);
			} else {
				/* FS_VIRTUAL - call setfullscreen to recalc vout geometry */
				setfullscreen(c, 1);
			}
		}

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = 1;

		config_head->state.x = m->monitor_area.x;
		config_head->state.y = m->monitor_area.y;

		if (!selmon) {
			selmon = m;
		}
	}

	if (selmon && selmon->wlr_output->enabled) {
		VirtualOutput *vout = focusvout(selmon);
		wl_list_for_each(c, &clients, link) {
			if (!vt_recovery_mode && !c->mon && client_surface(c)->mapped && vout && vout->ws)
				setworkspace(c, vout->ws);
		}
		focusclient(focustop(selmon), 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface,
					wlr_seat_get_keyboard(seat));
			client_activate_surface(selmon->lock_surface->surface, 1);
		}
	}

		/* FIXME: figure out why the cursor image is at 0,0 after turning all
		 * the monitors on.
		 * Move the cursor image where it used to be. It does not generate a
		 * wl_pointer.motion event for the clients, it's only the image what it's
		 * at the wrong position after all. */
		wlr_cursor_move(cursor, NULL, 0, 0);
		cursorsync();

		wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	Monitor *m = CLIENT_MON(c);
	VirtualOutput *vout = CLIENT_VOUT(c);
	struct wlr_box area = {0};

	if (c == focustop(c->mon))
		updateipc();

	if (!m || !vout || !vout->tabhdr)
		return;
	if (!vout->lt[vout->sellt] || vout->lt[vout->sellt]->arrange != tabbed)
		return;
	if (vout->layout_geom.width > 0 && vout->layout_geom.height > 0)
		area = vout->layout_geom;
	else
		area = m->window_area;
	tabhdrupdate(m, vout, area, focustopvout(vout));
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;
	updateipc();

	if (client_surface(c)->mapped)
		client_set_border_color(c, urgentcolor);
}

void
view(const Arg *arg)
{
	Workspace *ws;
	VirtualOutput *vout;
	VirtualOutput *target = NULL;
	if (!selmon)
		return;
	if (arg->ui >= WORKSPACE_COUNT)
		return;
	ws = wsbyid(arg->ui);
	if (!ws)
		return;
	vout = ws->vout;
	if (!vout) {
		if (cursor) {
			Monitor *pointer_mon = xytomon(cursor->x, cursor->y);
			VirtualOutput *pointer_vout = pointer_mon ? voutat(pointer_mon, cursor->x, cursor->y) : NULL;
			if (pointer_vout)
				target = pointer_vout;
			else if (pointer_mon)
				target = focusvout(pointer_mon);
		}
		if (!target && selmon)
			target = focusvout(selmon);
		if (!target)
			return;
		wsattach(target, ws);
		vout = ws->vout;
	}
	if (!vout)
		return;
	if (vout->mon) {
		selmon = vout->mon;
		selvout = vout;
		selws = ws;
	}
	wsactivate(vout, ws, 1);
	if (vout->mon) {
		/* always update focus when switching to a different vout, even if
		 * the workspace was already active on that vout */
		if (vout->ws == ws && vout->mon == selmon) {
			focusclient(focustopvout(vout), 1);
		}
		cursorwarptovout(vout);
	}
	updateipc();
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER)
			surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node))->surface;
		/* Walk the tree to find a node that knows the client */
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			c = NULL;
			l = pnode->data;
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	VirtualOutput *vout = focusvout(selmon);

	if (!sel || !vout || !vout->lt[vout->sellt]->arrange || sel->isfloating)
		return;
	if (tiling_locked_by_fullscreen(sel))
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}

static Workspace *
wsbyid(unsigned int id)
{
	return id < WORKSPACE_COUNT ? &workspaces[id] : NULL;
}

static VirtualOutput *
firstvout(Monitor *m)
{
	VirtualOutput *vout;
	if (!m || wl_list_empty(&m->vouts)) {
		return NULL;
	}
	vout = wl_container_of(m->vouts.next, vout, link);
	return vout;
}

static VirtualOutput *
findvoutbyname(Monitor *m, const char *name)
{
	VirtualOutput *vout;
	if (!m || !name || !*name)
		return NULL;
	wl_list_for_each(vout, &m->vouts, link) {
		if (!strcmp(vout->name, name)) {
			return vout;
		}
	}
	return NULL;
}

VirtualOutput *
focusvout(Monitor *m)
{
	VirtualOutput *vout;
	if (!m) {
		return NULL;
	}
	vout = m->focus_vout;
	if (!vout || vout->mon != m)
		vout = firstvout(m);
	return vout;
}

VirtualOutput *
voutat(Monitor *m, double lx, double ly)
{
	VirtualOutput *vout;
	if (!m)
		return NULL;
	wl_list_for_each(vout, &m->vouts, link) {
		if (vout->layout_geom.width <= 0 || vout->layout_geom.height <= 0)
			continue;
		if (lx >= vout->layout_geom.x && lx < vout->layout_geom.x + vout->layout_geom.width &&
			ly >= vout->layout_geom.y && ly < vout->layout_geom.y + vout->layout_geom.height)
			return vout;
	}
	return focusvout(m);
}

void
arrangevout(Monitor *m, const struct wlr_box *usable_area)
{
	VirtualOutput *vout;
	struct wlr_box base;
	struct wlr_box geom;

	if (!m)
		return;

	base = usable_area ? *usable_area : m->window_area;

	wl_list_for_each(vout, &m->vouts, link) {
		geom = base;
		if (vout->rule_geom.width > 0 && vout->rule_geom.height > 0) {
			geom.x = base.x + vout->rule_geom.x;
			geom.y = base.y + vout->rule_geom.y;
			geom.width = vout->rule_geom.width;
			geom.height = vout->rule_geom.height;
		} else {
			geom.x = base.x + vout->rule_geom.x;
			geom.y = base.y + vout->rule_geom.y;
			if (vout->rule_geom.width > 0)
				geom.width = vout->rule_geom.width;
			else
				geom.width = base.width - vout->rule_geom.x;
			if (vout->rule_geom.height > 0)
				geom.height = vout->rule_geom.height;
			else
				geom.height = base.height - vout->rule_geom.y;
		}

		if (geom.x < base.x) {
			geom.width -= base.x - geom.x;
			geom.x = base.x;
		}
		if (geom.y < base.y) {
			geom.height -= base.y - geom.y;
			geom.y = base.y;
		}
		if (geom.x + geom.width > base.x + base.width)
			geom.width = (base.x + base.width) - geom.x;
		if (geom.y + geom.height > base.y + base.height)
			geom.height = (base.y + base.height) - geom.y;

		if (geom.width <= 0 || geom.height <= 0)
			geom = base;

		vout->layout_geom = geom;
	}
}

static VirtualOutput *
createvout(Monitor *m, const char *name)
{
	static unsigned int next_id = 1;
	VirtualOutput *vout;
	const VirtualOutputRule *default_rule = NULL;
	size_t j;

	if (!m)
		return NULL;

	/* find the default (fallback) vout rule */
	for (j = 0; j < LENGTH(vorules); j++) {
		if (!vorules[j].monitor) {
			default_rule = &vorules[j];
			break;
		}
	}

	vout = ecalloc(1, sizeof(*vout));
	vout->mon = m;
	vout->id = next_id++;
	vout->layout_geom = m->window_area;
	vout->rule_geom = (struct wlr_box){0};
	vout->mfact = default_rule && default_rule->mfact > 0 ? default_rule->mfact : 0.55f;
	vout->nmaster = default_rule && default_rule->nmaster > 0 ? default_rule->nmaster : 1;
	vout->sellt = 0;
	vout->lt[0] = &layouts[0];
	vout->lt[1] = (LENGTH(layouts) > 1) ? &layouts[1] : vout->lt[0];
	strncpy(vout->name, name ? name : m->wlr_output->name, sizeof(vout->name) - 1);
	vout->name[sizeof(vout->name) - 1] = '\0';
	strncpy(vout->ltsymbol, vout->lt[vout->sellt]->symbol, LENGTH(vout->ltsymbol));
	vout->ltsymbol[LENGTH(vout->ltsymbol) - 1] = '\0';
	if (wl_list_empty(&m->vouts))
		wl_list_insert(&m->vouts, &vout->link);
	else
		wl_list_insert(m->vouts.prev, &vout->link);
	wl_list_init(&vout->workspaces);
	vout->tabhdr = wlr_scene_tree_create(layers[LyrFloat]);
	if (vout->tabhdr)
		wlr_scene_node_set_enabled(&vout->tabhdr->node, 0);
	if (!m->focus_vout)
		m->focus_vout = vout;
	return vout;
}

static void
destroyvout(VirtualOutput *vout)
{
	Workspace *ws, *tmp;
	Monitor *m;
	if (!vout)
		return;
	m = vout->mon;
	if (vout->ws)
		wssave(vout);
	wl_list_for_each_safe(ws, tmp, &vout->workspaces, link) {
		wl_list_remove(&ws->link);
		wl_list_init(&ws->link);
		ws->vout = NULL;
	}
	if (vout->tabhdr)
		wlr_scene_node_destroy(&vout->tabhdr->node);
	wl_list_remove(&vout->link);
	if (m && m->focus_vout == vout)
		m->focus_vout = firstvout(m);
	free(vout);
}

static Client *
focustopvout(VirtualOutput *vout)
{
	Client *c;
	if (!vout)
		return NULL;
	wl_list_for_each(c, &fstack, flink) {
		if (c->ws && c->ws == vout->ws && !client_is_unmanaged(c))
			return c;
	}
	wl_list_for_each(c, &clients, link) {
		if (c->ws && c->ws == vout->ws)
			return c;
	}
	return NULL;
}

static Workspace *
wsfindfree(void)
{
	unsigned int i;
	for (i = 0; i < WORKSPACE_COUNT; i++)
		if (!workspaces[i].vout)
			return &workspaces[i];
	return NULL;
}

static void
wsinsert(VirtualOutput *vout, Workspace *ws)
{
	Workspace *iter;
	if (!vout)
		return;
	if (wl_list_empty(&vout->workspaces)) {
		wl_list_insert(&vout->workspaces, &ws->link);
		return;
	}
	wl_list_for_each(iter, &vout->workspaces, link) {
		if (ws->id < iter->id) {
			wl_list_insert(iter->link.prev, &ws->link);
			return;
		}
	}
	wl_list_insert(vout->workspaces.prev, &ws->link);
}

static void
wssave(VirtualOutput *vout)
{
	Workspace *ws;
	if (!vout) {
		return;
	}
	ws = vout->ws;
	if (!ws)
		return;
	ws->state.mfact = vout->mfact;
	ws->state.nmaster = vout->nmaster;
	ws->state.sellt = vout->sellt;
	ws->state.lt[0] = vout->lt[0];
	ws->state.lt[1] = vout->lt[1];
}

static void
wsload(VirtualOutput *vout, Workspace *ws)
{
	if (!vout || !ws)
		return;
	vout->ws = ws;
	vout->mfact = ws->state.mfact;
	vout->nmaster = ws->state.nmaster;
	vout->sellt = ws->state.sellt;
	vout->lt[0] = ws->state.lt[0] ? ws->state.lt[0] : &layouts[0];
	vout->lt[1] = ws->state.lt[1] ? ws->state.lt[1] : vout->lt[0];
	if (vout->sellt > 1)
		vout->sellt = 0;
	strncpy(vout->ltsymbol, vout->lt[vout->sellt]->symbol, LENGTH(vout->ltsymbol));
}

static Workspace *
wsfirst(VirtualOutput *vout)
{
	Workspace *ws;
	if (!vout || wl_list_empty(&vout->workspaces))
		return NULL;
	ws = wl_container_of(vout->workspaces.next, ws, link);
	return ws;
}

static Workspace *
wsnext(VirtualOutput *vout, Workspace *exclude)
{
	Workspace *ws;
	if (!vout)
		return NULL;
	wl_list_for_each(ws, &vout->workspaces, link) {
		if (ws != exclude)
			return ws;
	}
	return NULL;
}

static void
wsattach(VirtualOutput *vout, Workspace *ws)
{
	VirtualOutput *old;
	if (!vout || !ws)
		return;
	if (ws->vout == vout)
		return;
	old = ws->vout;
	if (old) {
		if (old->ws == ws)
			wssave(old);
		wl_list_remove(&ws->link);
	}
	ws->vout = vout;
	wsinsert(vout, ws);
}

static void
wsactivate(VirtualOutput *vout, Workspace *ws, int focus_change)
{
	Workspace *old;
	Monitor *m;
	/* char vbuf[64]; - unused */
	if (!vout)
		return;
	m = vout->mon;
	if (m)
		m->focus_vout = vout;
	old = vout->ws;
	if (old == ws)
		return;
	wssave(vout);
	if (ws && ws->vout != vout)
		wsattach(vout, ws);
	if (ws) {
		wsload(vout, ws);
		if (m == selmon) {
			selws = ws;
			selvout = vout;
		}
		arrange(m);
		if (focus_change)
			focusclient(focustopvout(vout), 1);
	} else {
		vout->ws = NULL;
		if (m == selmon) {
			selws = NULL;
			selvout = vout;
		}
		arrange(m);
		if (focus_change)
			focusclient(focustopvout(vout), 1);
	}
}

static void
wsmoveto(Workspace *ws, VirtualOutput *vout)
{
	VirtualOutput *old;
	Workspace *fallback;
	Monitor *old_mon;
	/* char newbuf[64], oldbuf[64]; - unused */
	if (!ws || !vout || ws->vout == vout)
		return;
	old = ws->vout;
	old_mon = old ? old->mon : NULL;
	wsattach(vout, ws);
	wsactivate(vout, ws, 1);
	if (ws) {
		Client *c;
		wl_list_for_each(c, &clients, link) {
			if (c->ws == ws)
				setworkspace(c, ws);
		}
	}
	if (old) {
		fallback = wsnext(old, ws);
		if (!fallback)
			fallback = wsfirst(old);
		if (!fallback) {
			fallback = wsfindfree();
			if (fallback)
				wsattach(old, fallback);
		}
		wsactivate(old, fallback, old_mon && old_mon == selmon);
	}
}

#ifdef XWAYLAND
void
activatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, activate);

	/* Only "managed" windows can be activated */
	if (!client_is_unmanaged(c))
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void
associatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, associate);

	LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
	LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

void
configurex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	VirtualOutput *vout;

	if (!client_surface(c) || !client_surface(c)->mapped) {
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if (client_is_unmanaged(c)) {
		wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	vout = CLIENT_VOUT(c);
	if ((c->isfloating && c != grabc) || !vout || !vout->lt[vout->sellt]->arrange) {
		resize(c, (struct wlr_box){.x = event->x - c->bw,
				.y = event->y - c->bw, .width = event->width + c->bw * 2,
				.height = event->height + c->bw * 2}, 0);
	} else {
		arrange(c->mon);
	}
}

void
createnotifyx11(struct wl_listener *listener, void *data)
{
	struct wlr_xwayland_surface *xsurface = data;
	Client *c;

	/* Allocate a Client for this surface */
	c = xsurface->data = ecalloc(1, sizeof(*c));
	c->surface.xwayland = xsurface;
	c->type = X11;
	c->bw = client_is_unmanaged(c) ? 0 : borderpx;

	/* Listen to the various events it can emit */
	LISTEN(&xsurface->events.associate, &c->associate, associatex11);
	LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
	LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
	LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
	LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
	LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
	LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

void
dissociatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, dissociate);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
}

void
sethints(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_hints);
	struct wlr_surface *surface = client_surface(c);
	if (c == focustop(selmon) || !c->surface.xwayland->hints)
		return;

	c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
	updateipc();

	if (c->isurgent && surface && surface->mapped)
		client_set_border_color(c, urgentcolor);
}

void
xwaylandready(struct wl_listener *listener, void *data)
{
	struct wlr_xcursor *xcursor;

	/* assign the one and only seat */
	wlr_xwayland_set_seat(xwayland, seat);

	/* Set the default XWayland cursor to match the rest of dwl. */
	if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
		wlr_xwayland_set_cursor(xwayland,
				xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
				xcursor->images[0]->width, xcursor->images[0]->height,
				xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);
}
#endif

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:hdv")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else if (c == 'd')
			log_level = WLR_DEBUG;
		else if (c == 'v')
			die("dwl " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
