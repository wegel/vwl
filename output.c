/* Physical monitor discovery, removal, layout, and focus. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include "vwl.h"
#include "ipc.h"
#include "util.h"

struct wl_list mons;
Monitor *selmon;

static void cleanupmon(struct wl_listener *listener, void *data);
static void closemon(Monitor *m);
static int outputmatches(struct wlr_output *o, const char *pattern);
static int invertdir(enum wlr_direction dir);
static Monitor *monfirstenabled(void);
static int monisvalid(Monitor *mon);

struct wlr_box sgeom;
bool vt_recovery_mode;
static struct wlr_scene_rect *root_bg;
static struct wlr_output_manager_v1 *output_mgr;

void
output_init(struct wlr_scene_rect *background)
{
	root_bg = background;
	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);
}

static void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	size_t i;

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link) wlr_layer_surface_v1_destroy(l->layer_surface);
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

	/* closemon() only repairs selmon while it still points at m, and the
	 * re-entrant updatemons() above can hand it back. Drop every global
	 * reference before the Monitor goes away. */
	if (selmon == m)
		selmon = monfirstenabled();
	if (hover.mon == m)
		hover.mon = NULL;
	if (cursor_phys.mon == m)
		cursor_phys.mon = NULL;
	if (cursor_phys.last_mon == m)
		cursor_phys.last_mon = NULL;

	free(m);
}

static void
closemon(Monitor *m)
{
	/* Keep each workspace's home while its monitor is unavailable. */
	Client *c;
	Workspace *ws, *wtmp;
	VirtualOutput *vout, *vtmp;
	Monitor *target;
	VirtualOutput *target_vout = NULL;
	vt_recovery_mode = true;
	if (selmon == m || !monisvalid(selmon) || !selmon->wlr_output->enabled) {
		selmon = NULL;
		wl_list_for_each(target, &mons, link) {
			if (target != m && target->wlr_output->enabled) {
				selmon = target;
				break;
			}
		}
	}

	target = selmon;
	selvout = target ? focusedvout(target) : NULL;
	if (target)
		target_vout = focusedvout(target);
	/* Save the active workspaces before moving any of them to another output. */
	wl_list_for_each(vout, &m->vouts, link) {
		wssave(vout);
		wl_list_for_each(ws, &vout->workspaces, link) {
			if (!ws->was_orphaned) {
				snprintf(ws->orphan_vout_name, sizeof(ws->orphan_vout_name), "%s", vout->name);
				snprintf(ws->orphan_monitor_name, sizeof(ws->orphan_monitor_name), "%s",
						m->wlr_output->name);
				ws->orphan_was_active = vout->ws == ws;
				ws->was_orphaned = true;
			}
		}
	}
	wl_list_for_each_safe(vout, vtmp, &m->vouts, link) {
		wl_list_for_each_safe(ws, wtmp, &vout->workspaces, link) {
			if (target_vout) {
				wsattach(target_vout, ws);
				if (!target_vout->ws)
					wsactivate(target_vout, ws, 0);
			} else {
				wl_list_remove(&ws->link);
				wl_list_init(&ws->link);
				ws->vout = NULL;
			}
		}
		/* Detached workspaces keep their saved state, including the active one. */
		vout->ws = NULL;
		destroyvout(vout);
	}
	if (target && target_vout && !target_vout->ws)
		wsactivate(target_vout, wsfirst(target_vout), 0);

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m)
			setworkspace(c, c->ws);
	}
	selws = selvout ? selvout->ws : NULL;
	focusclient(focustop(selmon), 1);
	updateipc();
}

/* Match either the connector or the make/model/serial identifier by substring. */
static int
outputmatches(struct wlr_output *o, const char *pattern)
{
	const char *make = o->make ? o->make : "Unknown";
	const char *model = o->model ? o->model : "Unknown";
	const char *serial = o->serial ? o->serial : "Unknown";
	char *id;
	size_t size;
	int matched;

	if (strstr(o->name, pattern))
		return 1;
	size = strlen(make) + strlen(model) + strlen(serial) + 3;
	id = ecalloc(size, 1);
	snprintf(id, size, "%s %s %s", make, model, serial);
	matched = strstr(id, pattern) != NULL;
	free(id);
	return matched;
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
	const VirtualOutputRule *matched_rules[workspace_config.rule_count];
	int first;
	bool found_orphans = false;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
	wl_list_init(&m->vouts);

	for (i = 0; i < LENGTH(m->layers); i++) wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	for (r = output_config.rules; r < output_config.rules + output_config.rule_count; r++) {
		if (!r->name || outputmatches(wlr_output, r->name)) {
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

	wlr_output_state_set_enabled(&state, 1);
	if (!wlr_output_commit_state(wlr_output, &state)) {
		wlr_log(WLR_ERROR, "failed to commit initial state for output %s", wlr_output->name);
		wlr_output_state_finish(&state);
		wlr_output->data = NULL;
		free(m);
		return;
	}
	wlr_output_state_finish(&state);

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

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
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, output_config.fullscreen_color);
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
	for (i = 0; i < workspace_config.rule_count; i++) {
		const VirtualOutputRule *vr = &workspace_config.rules[i];
		if (!vr->monitor)
			continue;
		if (outputmatches(wlr_output, vr->monitor))
			matched_rules[matched_count++] = vr;
	}
	if (!matched_count) {
		for (i = 0; i < workspace_config.rule_count; i++) {
			const VirtualOutputRule *vr = &workspace_config.rules[i];
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
			attachvoutworkspaces(new_vout, vr->workspaces, vr->workspace_count);
		} else {
			new_vout = createvout(m, NULL);
		}
		if (!first_vout)
			first_vout = new_vout;
	}
	if (!first_vout)
		first_vout = firstvout(m);

	if (first) {
		if (!vt_recovery_mode) {
			wl_list_for_each(vout, &m->vouts, link) {
				Workspace *first_ws = wsfirst(vout);
				if (first_ws)
					wsactivate(vout, first_ws, 0);
			}
			selws = first_vout ? first_vout->ws : NULL;
		} else {
			selws = NULL;
		}
		selvout = first_vout;
	} else if (!vt_recovery_mode) {
		wl_list_for_each(vout, &m->vouts, link) {
			Workspace *first_ws = wsfirst(vout);
			if (first_ws)
				wsactivate(vout, first_ws, 0);
		}
	}

	/* Reattach any orphaned workspaces to this monitor */
	for (i = 0; i < WORKSPACE_COUNT; i++) {
		Workspace *ws = &workspaces[i];
		VirtualOutput *target_vout;
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
		if (ws->orphan_was_active)
			wsactivate(target_vout, ws, 0);
		ws->was_orphaned = false; /* Clear the flag after reattachment */
		ws->orphan_was_active = false;
		ws->orphan_vout_name[0] = '\0';
		ws->orphan_monitor_name[0] = '\0';
	}
	wl_list_for_each(vout, &m->vouts, link) {
		if (!vout->ws)
			wsactivate(vout, wsfirst(vout), 0);
	}

	if (found_orphans) {
		vt_recovery_mode = true;
	}

	if (selmon == m) {
		selvout = focusedvout(m);
		selws = selvout ? selvout->ws : NULL;
	}
	arrange(m);
	updateipc();
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
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
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
		if (m->wlr_output->enabled && !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	/* Make sure the clients are hidden when vwl is locked */
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
			wlr_session_lock_surface_v1_configure(
					m->lock_surface, m->monitor_area.width, m->monitor_area.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		usable = m->window_area;
		arrangevout(m, &usable);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = 1;

		config_head->state.x = m->monitor_area.x;
		config_head->state.y = m->monitor_area.y;

		if (!selmon) {
			selmon = m;
		}
	}

	updatephysorigins();

	if (!monisvalid(selmon))
		selmon = monfirstenabled();

	if (selmon && selmon->wlr_output->enabled) {
		VirtualOutput *vout = focusedvout(selmon);
		wl_list_for_each(c, &clients, link) {
			if (!vt_recovery_mode && !c->mon && client_surface(c)->mapped && vout && vout->ws)
				setworkspace(c, vout->ws);
		}
		focusclient(focustop(selmon), 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface, wlr_seat_get_keyboard(seat));
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

Monitor *
monitorbyname(const char *name)
{
	Monitor *m;

	if (!name)
		return NULL;

	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output && !strcmp(m->wlr_output->name, name))
			return m;
	}

	return NULL;
}

static int
invertdir(enum wlr_direction dir)
{
	int inverter = 0;
	if (dir & (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT))
		inverter |= (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT);
	if (dir & (WLR_DIRECTION_DOWN | WLR_DIRECTION_UP))
		inverter |= (WLR_DIRECTION_DOWN | WLR_DIRECTION_UP);
	return dir ^ inverter;
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	return dirtomonfrom(selmon, dir);
}

Monitor *
dirtomonfrom(Monitor *from, enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!from || !wlr_output_layout_get(output_layout, from->wlr_output))
		return from;
	if ((next = wlr_output_layout_adjacent_output(
			     output_layout, dir, from->wlr_output, from->monitor_area.x, from->monitor_area.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout, invertdir(dir), from->wlr_output,
			     from->monitor_area.x, from->monitor_area.y)))
		return next->data;
	return from;
}

static Monitor *
monfirstenabled(void)
{
	Monitor *m;

	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output && m->wlr_output->enabled)
			return m;
	}
	return NULL;
}

static int
monisvalid(Monitor *mon)
{
	Monitor *m;

	if (!mon)
		return 0;

	wl_list_for_each(m, &mons, link) {
		if (m == mon)
			return m->wlr_output != NULL;
	}
	return 0;
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
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
		selvout = focusedvout(selmon);
		selws = selvout ? selvout->ws : NULL;
	}
	focusclient(focustop(selmon), 1);
}
