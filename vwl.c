/*
 * See LICENSE file for copyright and license details.
 */
#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdbool.h>
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
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
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
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
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
#include <wlr/util/box.h>
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
#include "ipc.h"
#include "share.h"
#include "spawnrules.h"
#include "tabhdr.h"
#include "util.h"

static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
static void commitnotify(struct wl_listener *listener, void *data);
static unsigned int borderwidth(Client *c);
static const float *bordercolorfor(Client *c, int focused);
static void updatebordercolor(Client *c, int focused);
static void updateborderwidth(Client *c, int preserve_content);
static void placechild(Client *c, Client *parent, Workspace *ws);
static void placefloating(Client *c);
static void destroynotify(struct wl_listener *listener, void *data);
static bool tiling_locked_by_fullscreen(Client *sel);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void run(const char *startup_cmd);
static void setfloating(Client *c, int floating);
static struct wlr_box fullscreenbox(Client *c, int mode);
static bool hasvirtualfullscreen(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setup(void);
static pid_t spawn_argv(const char *const argv[]);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void associatex11(struct wl_listener *listener, void *data);
static void setstartupidx11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void dissociatex11(struct wl_listener *listener, void *data);
static void sethints(struct wl_listener *listener, void *data);
#endif

static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;

static struct wlr_session_lock_manager_v1 *session_lock_mgr;

/* configuration, allows nested code to access above variables */
#include "config.h"

const struct WorkspaceConfig workspace_config = {layouts, LENGTH(layouts), vorules, LENGTH(vorules)};
const struct OutputConfig output_config = {monrules, LENGTH(monrules), fullscreen_bg};
const struct CursorConfig cursor_config = {sloppyfocus, enable_cursor_warp_to_vout, enable_physical_cursor_gap_jumps};

static const struct TabHdrStyle tabhdr_style_data = {
		.position = tabhdr_position,
		.active_color = tabhdr_active_color,
		.inactive_color = tabhdr_inactive_color,
		.text_active_color = tabhdr_text_active_color,
		.text_inactive_color = tabhdr_text_inactive_color,
		.font = tabhdr_font,
		.padding_top = tabhdr_padding_top,
		.padding_bottom = tabhdr_padding_bottom,
		.padding_left = tabhdr_padding_left,
		.padding_right = tabhdr_padding_right,
		.title_transforms = tabhdr_title_transforms,
};

const struct TabHdrStyle *
tabhdr_style(void)
{
	return &tabhdr_style_data;
}

/* function implementations */
static void
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

static void
applyrules(Client *c)
{
	/* rule matching */
	const char *appid, *title;
	unsigned int workspace_id = WORKSPACE_COUNT;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;
	VirtualOutput *target_vout = NULL;
	Workspace *ws = NULL;

	appid = client_get_appid(c);
	title = client_get_title(c);

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title)) && (!r->id || strstr(appid, r->id))) {
			if (r->workspace < WORKSPACE_COUNT)
				workspace_id = r->workspace;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}
	if (!mon)
		mon = selmon;
	if (mon)
		target_vout = focusedvout(mon);
	if (workspace_id < WORKSPACE_COUNT)
		ws = wsbyid(workspace_id);
	if (ws && target_vout && !ws->vout)
		wsattach(target_vout, ws);
	if (!ws && mon)
		ws = MON_FOCUS_WS(mon);
	if (ws && target_vout && ws->vout && ws->vout->mon != mon)
		wsmoveto(ws, target_vout);
	if (!ws && selmon)
		ws = MON_FOCUS_WS(selmon);
	if (!ws)
		ws = selws;
	if (!ws && target_vout) {
		ws = wsfindfree();
		if (ws) {
			wsattach(target_vout, ws);
			wsactivate(target_vout, ws, 0);
		}
	}
	setworkspace(c, ws);
}

void
arrange(Monitor *m)
{
	Client *c, *fs_client;
	VirtualOutput *vout;
	VirtualOutput *prev_focus = focusedvout(m);

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			if (c->isfullscreen && c->fullscreen_mode != FS_NONE) {
				struct wlr_box target = fullscreenbox(c, c->fullscreen_mode);
				if (!wlr_box_equal(&c->geom, &target))
					resize(c, target, 0);
			}
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m) && !share_is_captured(c));
			if (!c->isfullscreen && !client_is_unmanaged(c))
				wlr_scene_node_reparent(
						&c->scene->node, c->isfloating ? layers[LyrTop] : layers[LyrTile]);
		}
	}

	/* hide non-fullscreen clients when a fullscreen client exists on each vout */
	wl_list_for_each(vout, &m->vouts, link) {
		fs_client = NULL;
		wl_list_for_each(c, &fstack, flink) {
			if (c->ws == vout->ws && c->isfullscreen && !client_is_unmanaged(c)) {
				fs_client = c;
				break;
			}
		}
		if (fs_client) {
			wl_list_for_each(c, &clients, link) {
				if (c->ws == vout->ws && c != fs_client && !client_is_unmanaged(c)) {
					wlr_scene_node_set_enabled(&c->scene->node, 0);
					client_set_suspended(c, !share_is_captured(c));
				}
			}
		}
	}

	c = focustop(m);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, c && c->isfullscreen && c->fullscreen_mode == FS_MONITOR);

	wl_list_for_each(vout, &m->vouts, link) {
		m->focus_vout = vout;
		strncpy(vout->ltsymbol, vout->lt[vout->sellt]->symbol, LENGTH(vout->ltsymbol));
		vout->ltsymbol[LENGTH(vout->ltsymbol) - 1] = '\0';

		if (vout->lt[vout->sellt]->arrange) {
			vout->lt[vout->sellt]->arrange(m);
			if (vout->lt[vout->sellt]->arrange != tabbed)
				tabhdr_disable(vout);
		} else {
			tabhdr_disable(vout);
		}
	}
	m->focus_vout = prev_focus ? prev_focus : firstvout(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

static void
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

		wlr_xdg_toplevel_set_wm_capabilities(
				c->surface.xdg->toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, 0);

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;
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
	c->bw = 0;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

static unsigned int
borderwidth(Client *c)
{
	if (!c || client_is_unmanaged(c) || c->isfullscreen)
		return 0;
	return client_is_x11(c) ? xborderpx : c->isfloating ? floatborderpx : borderpx;
}

static const float *
bordercolorfor(Client *c, int focused)
{
	if (c->isurgent)
		return client_is_x11(c) ? xurgentcolor : c->isfloating ? floaturgentcolor : urgentcolor;
	if (focused)
		return client_is_x11(c) ? xfocuscolor : c->isfloating ? floatfocuscolor : focuscolor;
	return client_is_x11(c) ? xbordercolor : c->isfloating ? floatbordercolor : bordercolor;
}

static void
updatebordercolor(Client *c, int focused)
{
	if (!c || client_is_unmanaged(c))
		return;
	client_set_border_color(c, bordercolorfor(c, focused));
}

static void
updateborderwidth(Client *c, int preserve_content)
{
	struct wlr_box geo;
	int delta;
	unsigned int new_bw;

	if (!c)
		return;
	new_bw = borderwidth(c);
	if (c->bw == new_bw)
		return;
	if (!c->mon || !client_surface(c)->mapped) {
		c->bw = new_bw;
		return;
	}

	geo = c->geom;
	delta = (int)new_bw - (int)c->bw;
	c->bw = new_bw;
	if (preserve_content && delta != 0) {
		geo.width += 2 * delta;
		geo.height += 2 * delta;
	}
	resize(c, geo, 0);
}

/* Place a new dialog once, before setworkspace() displays it. */
static void
placechild(Client *c, Client *parent, Workspace *ws)
{
	VirtualOutput *vout = ws ? ws->vout : NULL;
	Monitor *mon = vout ? vout->mon : parent->mon;
	struct wlr_box area;

	/* X11 supplies screen coordinates; xdg geometry offsets are surface-local. */
	if (!c->isfloating || (client_is_x11(c) && (c->geom.x != 0 || c->geom.y != 0)))
		return;
	c->geom.x = parent->geom.x + (parent->geom.width - c->geom.width) / 2;
	c->geom.y = parent->geom.y + (parent->geom.height - c->geom.height) / 2;
	if (!mon)
		return;
	area = vout ? voutarea(vout) : mon->window_area;
	/* Keep the whole dialog visible if it fits; otherwise expose its top left. */
	c->geom.x = MAX(area.x, MIN(c->geom.x, area.x + area.width - c->geom.width));
	c->geom.y = MAX(area.y, MIN(c->geom.y, area.y + area.height - c->geom.height));
}

static void
placefloating(Client *c)
{
	struct wlr_box area, geo;
	VirtualOutput *vout;
	const Layout *layout;
	int max_width, max_height;

	if (!c || !c->mon || !client_surface(c)->mapped)
		return;
	vout = CLIENT_VOUT(c);
	area = vout ? voutarea(vout) : c->mon->window_area;

	layout = vout ? vout->lt[vout->sellt] : NULL;
	if (layout && layout->arrange == tabbed)
		area = tabhdr_client_area(area);

	geo = c->geom;
	max_width = area.width;
	max_height = area.height;
	/* Configured factors cap the initial size, not a size chosen by the user. */
	if (!c->float_size_saved) {
		if (floating_window_width_factor > 0.0f && floating_window_width_factor <= 1.0f)
			max_width = (int)roundf(area.width * floating_window_width_factor);
		if (floating_window_height_factor > 0.0f && floating_window_height_factor <= 1.0f)
			max_height = (int)roundf(area.height * floating_window_height_factor);
	}
	if (c->float_width > 0)
		geo.width = c->float_width + 2 * (int)c->bw;
	if (c->float_height > 0)
		geo.height = c->float_height + 2 * (int)c->bw;
	geo.width = MAX((int)(2 * c->bw + 1), MIN(geo.width, max_width));
	geo.height = MAX((int)(2 * c->bw + 1), MIN(geo.height, max_height));
	if (center_floating_windows) {
		geo.x = area.x + (area.width - geo.width) / 2;
		geo.y = area.y + (area.height - geo.height) / 2;
	}
	geo.x = MAX(area.x, MIN(geo.x, area.x + area.width - geo.width));
	geo.y = MAX(area.y, MIN(geo.y, area.y + area.height - geo.height));

	resize(c, geo, 0);
}

static void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
	Client *c = wl_container_of(listener, c, destroy);
	spawnrules_forget(c);
	share_destroy_capture_scene(c);
	if (c->image_capture_source) {
		wl_list_remove(&c->image_capture_source_destroy.link);
		wl_list_init(&c->image_capture_source_destroy.link);
		c->image_capture_source = NULL;
	}
	share_destroy(c);
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
		wl_list_remove(&c->set_startup_id.link);
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
		if (vout && vout->tabhdr && vout->lt[vout->sellt] && vout->lt[vout->sellt]->arrange == tabbed &&
				client_is_virtual_fullscreen(c))
			wlr_scene_node_raise_to_top(&vout->tabhdr->node);
	}

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link) wlr_xdg_popup_destroy(popup);
	}

	/* Put the new client atop the focus stack and select its monitor */
	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		selvout = CLIENT_VOUT(c);
		if (selvout && selvout->mon != selmon)
			selvout = focusedvout(selmon);
		if (selvout && selvout->mon == selmon)
			selws = selvout->ws;
		if (selmon && selvout)
			selmon->focus_vout = selvout;
		if (selvout && selvout->lt[selvout->sellt] && selvout->lt[selvout->sellt]->arrange == tabbed) {
			struct wlr_box area;
			tabbed_mon = selvout->mon;
			area = (selvout->layout_geom.width && selvout->layout_geom.height) ? selvout->layout_geom
											   : selvout->mon->window_area;
			tabhdr_update(selvout->mon, selvout, area, focustoptiledvout(selvout));
		}
		c->isurgent = 0;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag)
			updatebordercolor(c, 1);
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with focuscolor
		 * and focus it after the overlay is closed. */
		if (old_client_type == LayerShell &&
				wlr_scene_node_coords(&old_l->scene->node, &unused_lx, &unused_ly) &&
				old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
			/* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
		 * and probably other clients */
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			updatebordercolor(old_c, 0);

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
	VirtualOutput *vout = focusedvout(selmon);
	struct wl_list *link;
	bool restrict_to_vout = false;
	const Layout *layout;
	if (!sel || tiling_locked_by_fullscreen(sel))
		return;
	if (vout && vout->lt[vout->sellt] && vout->lt[vout->sellt]->arrange == tabbed)
		restrict_to_vout = true;
	layout = vout ? vout->lt[vout->sellt] : NULL;
	c = sel;
	for (link = arg->i > 0 ? sel->link.next : sel->link.prev; link != &sel->link;
			link = arg->i > 0 ? link->next : link->prev) {
		Client *next;
		if (link == &clients)
			continue;
		next = wl_container_of(link, next, link);
		if (restrict_to_vout && CLIENT_VOUT(next) != vout)
			continue;
		if (layout && layout->arrange == tabbed && next->isfloating)
			continue;
		if (VISIBLEON(next, selmon)) {
			c = next;
			break;
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client *
focustop(Monitor *m)
{
	return focustopvout(focusedvout(m));
}

static void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

static void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;
	int i;
	int requested_fullscreen = c->isfullscreen;

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	/* Enabled later by a call to arrange() */
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
					       : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;
	if (!share_create_capture_scene(c))
		wlr_log(WLR_ERROR, "failed to create image capture scene for client");

	client_get_geometry(c, &c->geom);
	if (c->float_width <= 0)
		c->float_width = c->geom.width;
	if (c->float_height <= 0)
		c->float_height = c->geom.height;

	/* Handle unmanaged clients first so we can return prior create borders */
	if (client_is_unmanaged(c)) {
		spawnrules_forget(c);
		/* Unmanaged clients are placed above tiles but below fullscreen */
		wlr_scene_node_reparent(&c->scene->node, layers[LyrTop]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	/* Apply requests made before mapping after the normal window has a size
	 * and workspace, so leaving fullscreen can restore that window. */
	c->isfullscreen = 0;
	c->fullscreen_mode = FS_NONE;
	c->isfloating = client_is_float_type(c);
	c->bw = borderwidth(c);

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0, bordercolorfor(c, 0));
		c->border[i]->node.data = c;
	}

	/* Initialize client geometry with room for border */
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	/* Insert this client into client lists. */
	wl_list_insert(clients.prev, &c->link);
	wl_list_insert(&fstack, &c->flink);
	share_create_toplevel(c);

	/* Set initial workspace and focus:
	 * for clients with a parent, use the same workspace and monitor as parent.
	 * If there is no parent, apply rules */
	if ((p = client_get_parent(c))) {
		Workspace *target_ws = p->ws;
		spawnrules_forget(c);
		if (!target_ws && p->mon)
			target_ws = MON_FOCUS_WS(p->mon);
		placechild(c, p, target_ws);
		setworkspace(c, target_ws);
	} else if (!spawnrules_apply(c)) {
		applyrules(c);
	}
	if (requested_fullscreen)
		setfullscreen(c, 1);
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

static void
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
	if (c->surface.xdg->initialized && wl_resource_get_version(c->surface.xdg->toplevel->resource) < 5)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
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
	c->resize = client_set_size(c, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

static void
run(const char *startup_cmd)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);
	setenv("XDG_CURRENT_DESKTOP", "vwl:wlroots", 1);
	ipc_init();

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

static void
setfloating(Client *c, int floating)
{
	Client *p;

	if (!c || client_is_unmanaged(c) || c->isfullscreen)
		return;
	if (c->isfloating == floating)
		return;

	p = client_get_parent(c);
	if (!floating) {
		/* Save content dimensions before changing the floating border width. */
		c->float_width = MAX(1, c->geom.width - 2 * (int)c->bw);
		c->float_height = MAX(1, c->geom.height - 2 * (int)c->bw);
		c->float_size_saved = true;
	}
	c->isfloating = floating;
	updateborderwidth(c, floating);
	updatebordercolor(c, c == focustop(selmon));
	if (!c->mon || !client_surface(c)->mapped)
		return;
	if (floating)
		placefloating(c);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen || (p && p->isfullscreen) ? LyrFS
								 : c->isfloating		  ? LyrTop
												  : LyrTile]);
	arrange(c->mon);
	updateipc();
}

void
togglefloating(const Arg *arg)
{
	Client *c = focustop(selmon);

	if (!c || client_is_unmanaged(c) || c->isfullscreen)
		return;
	setfloating(c, !c->isfloating);
}

static struct wlr_box
fullscreenbox(Client *c, int mode)
{
	VirtualOutput *vout = CLIENT_VOUT(c);
	struct wlr_box target = c->mon ? c->mon->monitor_area : (struct wlr_box){0};

	if (mode != FS_VIRTUAL || !vout || wlr_box_empty(&vout->layout_geom))
		return target;
	target = vout->layout_geom;
	if (vout->lt[vout->sellt] && vout->lt[vout->sellt]->arrange == tabbed)
		target = tabhdr_client_area(target);
	return target;
}

static bool
hasvirtualfullscreen(Client *c)
{
	VirtualOutput *vout = CLIENT_VOUT(c);
	struct wlr_box virtual = fullscreenbox(c, FS_VIRTUAL);
	struct wlr_box physical = fullscreenbox(c, FS_MONITOR);

	/* Virtual fullscreen keeps tab switching, even with no visible header. */
	return !wlr_box_equal(&virtual, &physical) ||
	       (vout && vout->lt[vout->sellt] && vout->lt[vout->sellt]->arrange == tabbed);
}

static void
setfullscreen(Client *c, int fullscreen)
{
	int was_fullscreen = c->isfullscreen && c->fullscreen_mode != FS_NONE;

	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped) {
		if (!fullscreen) {
			if (was_fullscreen) {
				c->geom = c->prev;
				c->bw = borderwidth(c);
			}
			c->fullscreen_mode = FS_NONE;
		}
		return;
	}
	if (fullscreen && !was_fullscreen) {
		c->prev = c->geom;
		c->fullscreen_mode = hasvirtualfullscreen(c) ? FS_VIRTUAL : FS_MONITOR;
	}
	client_set_fullscreen(c, fullscreen);
	updateborderwidth(c, !fullscreen && !was_fullscreen);
	updatebordercolor(c, c == focustop(selmon));

	if (fullscreen) {
		if (c->fullscreen_mode == FS_MONITOR || !CLIENT_VOUT(c))
			wlr_scene_node_reparent(&c->scene->node, layers[LyrFS]);
		else
			wlr_scene_node_reparent(&c->scene->node, layers[LyrTop]);
		resize(c, fullscreenbox(c, c->fullscreen_mode), 0);
		if (c->fullscreen_mode == FS_VIRTUAL)
			wlr_scene_node_raise_to_top(&c->scene->node);
	} else {
		wlr_scene_node_reparent(&c->scene->node, layers[c->isfloating ? LyrTop : LyrTile]);
		c->fullscreen_mode = FS_NONE;
		if (was_fullscreen)
			resize(c, c->prev, 0);
	}
	arrange(c->mon);
	updateipc();
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

	if (oldmon && oldmon != c->mon)
		arrange(oldmon);

	if (c->mon) {
		resize(c, c->geom, 0);
		setfullscreen(c, c->isfullscreen);
		arrange(c->mon);
	}

	if (selmon && workspace_changed)
		focusclient(focustop(selmon), 1);
}

static void
setup(void)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE, SIGUSR1};
	struct wlr_scene_rect *root_bg;
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < (int)LENGTH(sig); i++) sigaction(sig[i], &sa, NULL);

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
	for (i = 0; i < NUM_LAYERS; i++) layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	workspaces_init();

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
		wlr_scene_set_linux_dmabuf_v1(scene, wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline && backend->features.timeline)
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
	wlr_ext_output_image_capture_source_manager_v1_create(dpy, 1);
	share_init(dpy, event_loop, alloc, drw);

	/* Handles startup tokens and client activation requests. */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);
	spawnrules_init(activation);

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
	locked_bg = wlr_scene_rect_create(
			layers[LyrBlock], sgeom.width, sgeom.height, (float[4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy), WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	cursor_init();

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
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard, &new_virtual_keyboard);
	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
	wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer, &new_virtual_pointer);

	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	output_init(root_bg);

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
	(void)spawn_argv((const char *const *)arg->v);
}

void
spawn_on_current_workspace(const Arg *arg)
{
	if (!selws)
		return;
	(void)spawnrules_spawn_on_workspace_argv(selws->id, (const char *const *)arg->v);
}

void
spawn_on_workspace(const Arg *arg)
{
	const SpawnWorkspaceArg *spawnarg = arg->v;

	if (!spawnarg)
		return;
	(void)spawnrules_spawn_on_workspace_argv(spawnarg->workspace_id, spawnarg->argv);
}

static pid_t
spawn_argv(const char *const argv[])
{
	pid_t pid;

	if ((pid = fork()) == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)argv)[0], (char **)argv);
		die("vwl: execvp %s failed:", ((char **)argv)[0]);
	}
	return pid;
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustopvout(selvout);
	if (!sel)
		return;
	if (!sel->isfullscreen) {
		setfullscreen(sel, 1);
	} else if (sel->fullscreen_mode == FS_VIRTUAL && hasvirtualfullscreen(sel)) {
		sel->fullscreen_mode = FS_MONITOR;
		setfullscreen(sel, 1);
	} else {
		setfullscreen(sel, 0);
	}
}

static void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}
	share_destroy_capture_scene(c);
	share_destroy(c);

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(selmon), 1);
		}
	} else {
		Monitor *m = c->mon;
		wl_list_remove(&c->link);
		wl_list_remove(&c->flink);
		/* Preserve workspace during VT recovery or when vout is NULL */
		if (!vt_recovery_mode && c->ws && c->ws->vout)
			setworkspace(c, NULL);
		else if (m)
			arrange(m);
	}

	updateipc();
	client_surface(c)->data = NULL;
	wlr_scene_node_destroy(&c->scene->node);
	c->scene = NULL;
	c->scene_surface = NULL;
	motionnotify(0, NULL, 0, 0, 0, 0);
}

static void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	Monitor *m = CLIENT_MON(c);
	VirtualOutput *vout = CLIENT_VOUT(c);
	struct wlr_box area = {0};

	share_update_title(c);

	if (c == focustop(c->mon))
		updateipc();

	if (!m || !vout || !vout->tabhdr)
		return;
	if (!vout->lt[vout->sellt] || vout->lt[vout->sellt]->arrange != tabbed)
		return;
	if (!wlr_box_empty(&vout->layout_geom))
		area = vout->layout_geom;
	else
		area = m->window_area;
	tabhdr_update(m, vout, area, focustoptiledvout(vout));
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	(void)listener;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (c && !client_surface(c)->mapped && spawnrules_bind_token(c, event->token))
		return;
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;
	updateipc();

	if (client_surface(c)->mapped)
		updatebordercolor(c, c == focustop(selmon));
}

void
xytonode(double x, double y, struct wlr_surface **psurface, Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_scene_surface *scene_surface;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER &&
				(scene_surface = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node))))
			surface = scene_surface->surface;
		/* Backgrounds and compositor-drawn buffers need not have an owner.
		 * Stop at the root instead of accessing its nonexistent parent. */
		for (pnode = node; pnode && !c; pnode = pnode->parent ? &pnode->parent->node : NULL) c = pnode->data;
		if (c && c->type == LayerShell) {
			l = (LayerSurface *)c;
			c = NULL;
		}
	}

	if (psurface)
		*psurface = surface;
	if (pc)
		*pc = c;
	if (pl)
		*pl = l;
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	VirtualOutput *vout = focusedvout(selmon);

	if (!sel || !vout || !vout->lt[vout->sellt]->arrange)
		return;
	if (sel->isfloating)
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

Client *
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

#ifdef XWAYLAND
static void
activatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, activate);

	/* Only "managed" windows can be activated */
	if (!client_is_unmanaged(c))
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

static void
associatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, associate);
	uint32_t group = c->surface.xwayland->hints ? c->surface.xwayland->hints->window_group : 0;

	spawnrules_track_startup_id(c->surface.xwayland->window_id, group, c->surface.xwayland->startup_id);
	LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
	LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

static void
setstartupidx11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_startup_id);
	uint32_t group = c->surface.xwayland->hints ? c->surface.xwayland->hints->window_group : 0;
	(void)data;

	if (!client_surface(c) || !client_surface(c)->mapped)
		spawnrules_track_startup_id(c->surface.xwayland->window_id, group, c->surface.xwayland->startup_id);
}

static void
configurex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	VirtualOutput *vout;

	if (!client_surface(c) || !client_surface(c)->mapped) {
		wlr_xwayland_surface_configure(c->surface.xwayland, event->x, event->y, event->width, event->height);
		return;
	}
	if (client_is_unmanaged(c)) {
		wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
		wlr_xwayland_surface_configure(c->surface.xwayland, event->x, event->y, event->width, event->height);
		return;
	}
	vout = CLIENT_VOUT(c);
	if (c->isfloating || !vout || !vout->lt[vout->sellt]->arrange) {
		resize(c,
				(struct wlr_box){.x = event->x - c->bw,
						.y = event->y - c->bw,
						.width = event->width + c->bw * 2,
						.height = event->height + c->bw * 2},
				0);
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
	c->bw = 0;

	/* Listen to the various events it can emit */
	LISTEN(&xsurface->events.associate, &c->associate, associatex11);
	LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
	LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
	LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
	LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
	LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
	LISTEN(&xsurface->events.set_startup_id, &c->set_startup_id, setstartupidx11);
	LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

static void
dissociatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, dissociate);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
}

static void
sethints(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_hints);
	struct wlr_surface *surface = client_surface(c);
	if (c == focustop(selmon) || !c->surface.xwayland->hints)
		return;

	c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
	updateipc();

	if (c->isurgent && surface && surface->mapped)
		updatebordercolor(c, c == focustop(selmon));
}

void
xwaylandready(struct wl_listener *listener, void *data)
{
	struct wlr_xcursor *xcursor;
	struct wlr_xcursor_image *image;
	struct wlr_buffer *buffer;

	/* assign the one and only seat */
	wlr_xwayland_set_seat(xwayland, seat);

	/* Set the default XWayland cursor to match the rest of vwl. */
	if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)) && xcursor->image_count > 0) {
		image = xcursor->images[0];
		buffer = wlr_xcursor_image_get_buffer(image);
		if (buffer)
			wlr_xwayland_set_cursor(xwayland, buffer, image->hotspot_x, image->hotspot_y);
	}
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
			die("vwl " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	wlr_log(WLR_INFO, "vwl " VERSION);
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
	return EXIT_FAILURE;
}
