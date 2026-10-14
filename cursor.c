/* Pointer motion, window dragging, and physical screen crossings. */
#include <float.h>
#include <math.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/region.h>
#include <wlr/util/log.h>
#include "vwl.h"
#include "ipc.h"
#include "capture.h"

HoverState hover;
int ipc_pointer_reveal_hover;
int ipc_pointer_reveal_edge;
unsigned int cursor_mode;
Client *grabc;
int grabcx, grabcy;
CursorPhysical cursor_phys;
struct wlr_pointer_constraint_v1 *active_constraint;

static int pointer_reveal_edge_for_cursor(Monitor *m, int current_edge);
static void update_pointer_reveal_state(void);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorinteg(double dx, double dy);
static int cursorgap(Monitor *origin, double prev_mm_x, double prev_mm_y);
static void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy, uint32_t time);

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static const int pointer_reveal_trigger_px = 2;
static const int pointer_reveal_hold_px = 40;

void
cursor_init(void)
{
	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);
	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);
}

void
cursorwarptovout(VirtualOutput *vout)
{
	struct wlr_box area;
	double cx, cy;

	if (!cursor_config.warp_to_vout || !cursor || !vout || !vout->mon)
		return;
	area = voutarea(vout);
	if (wlr_box_empty(&area))
		return;
	cx = area.x + area.width / 2.0;
	cy = area.y + area.height / 2.0;
	wlr_cursor_warp(cursor, NULL, cx, cy);
	cursorsync();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

static int
pointer_reveal_edge_for_cursor(Monitor *m, int current_edge)
{
	double rel_x, rel_y;
	double distances[4];
	int edges[4];
	double best = DBL_MAX;
	int best_edge = POINTER_REVEAL_EDGE_NONE;
	int i;

	if (!m || !cursor || m->monitor_area.width <= 0 || m->monitor_area.height <= 0)
		return POINTER_REVEAL_EDGE_NONE;

	rel_x = cursor->x - m->monitor_area.x;
	rel_y = cursor->y - m->monitor_area.y;
	if (rel_x < 0.0 || rel_y < 0.0 || rel_x > m->monitor_area.width || rel_y > m->monitor_area.height)
		return POINTER_REVEAL_EDGE_NONE;

	distances[0] = rel_y;
	distances[1] = m->monitor_area.height - rel_y;
	distances[2] = rel_x;
	distances[3] = m->monitor_area.width - rel_x;

	edges[0] = POINTER_REVEAL_EDGE_TOP;
	edges[1] = POINTER_REVEAL_EDGE_BOTTOM;
	edges[2] = POINTER_REVEAL_EDGE_LEFT;
	edges[3] = POINTER_REVEAL_EDGE_RIGHT;

	for (i = 0; i < 4; i++) {
		int edge = edges[i];
		double threshold = (edge == current_edge) ? pointer_reveal_hold_px : pointer_reveal_trigger_px;
		double d = distances[i];

		if (d < 0.0 || d > threshold)
			continue;
		if (d < best || (d == best && edge == current_edge)) {
			best = d;
			best_edge = edge;
		}
	}

	return best_edge;
}

static void
update_pointer_reveal_state(void)
{
	Monitor *m;
	int next_edge = POINTER_REVEAL_EDGE_NONE;
	int next_hover = 0;

	if (cursor) {
		m = xytomon(cursor->x, cursor->y);
		next_edge = pointer_reveal_edge_for_cursor(m, ipc_pointer_reveal_edge);
	}
	next_hover = next_edge != POINTER_REVEAL_EDGE_NONE;

	if (next_hover == ipc_pointer_reveal_hover && next_edge == ipc_pointer_reveal_edge)
		return;

	ipc_pointer_reveal_hover = next_hover;
	ipc_pointer_reveal_edge = next_edge;
	updateipc();
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy, double dx_unaccel, double dy_unaccel)
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

	if (cursor_mode == CurPressed && !seat->drag && surface != seat->pointer_state.focused_surface &&
			toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000, dx, dy, dx_unaccel, dy_unaccel);
		cursorinteg(dx, dy);

		if (cursor_config.physical_gap_jumps && prev_mon && cursor_mode == CurNormal && !seat->drag &&
				(!active_constraint || active_constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED) &&
				cursorgap(prev_mon, prev_mm_x, prev_mm_y)) {
			update_pointer_reveal_state();
			return;
		}

		wl_list_for_each(constraint, &pointer_constraints->constraints, link) cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy, sx + dx, sy + dy,
						    &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
					update_pointer_reveal_state();
					return;
				}
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		hover_mon = xytomon(cursor->x, cursor->y);
		hover_vout = NULL;
		if (hover_mon) {
			hover_vout = voutat(hover_mon, cursor->x, cursor->y);
		}
		hover.mon = hover_mon;
		hover.vout = hover_vout;
		hover.ws = hover_vout ? hover_vout->ws : NULL;
		xytonode(cursor->x, cursor->y, NULL, &hover.client, NULL, NULL, NULL);

		cursorsync();

		/* Update selmon (even while dragging a window) */
		if (cursor_config.sloppy_focus) {
			selmon = hover_mon;
			if (selmon) {
				if (hover_vout)
					selmon->focus_vout = hover_vout;
				selvout = hover_vout ? hover_vout : focusedvout(selmon);
				selws = selvout ? selvout->ws : NULL;
			}
		}
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	if (cursor_mode == CurMove) {
		resize(grabc,
				(struct wlr_box){.x = (int)round(cursor->x) - grabcx,
						.y = (int)round(cursor->y) - grabcy,
						.width = grabc->geom.width,
						.height = grabc->geom.height},
				1);
		cursorsync();
		update_pointer_reveal_state();
		return;
	} else if (cursor_mode == CurResize) {
		resize(grabc,
				(struct wlr_box){.x = grabc->geom.x,
						.y = grabc->geom.y,
						.width = (int)round(cursor->x) - grabc->geom.x,
						.height = (int)round(cursor->y) - grabc->geom.y},
				1);
		cursorsync();
		update_pointer_reveal_state();
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	pointerfocus(c, surface, sx, sy, time);
	cursorsync();
	update_pointer_reveal_state();
}

void
moveresize(const Arg *arg)
{
	VirtualOutput *vout;
	const Layout *layout;

	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	vout = CLIENT_VOUT(grabc);
	layout = vout ? vout->lt[vout->sellt] : NULL;
	if (layout && layout->arrange == tabbed && !grabc->isfloating)
		return;
	if (!grabc->isfloating)
		return;
	switch (cursor_mode = arg->ui) {
	case CurMove:
		grabcx = (int)round(cursor->x) - grabc->geom.x;
		grabcy = (int)round(cursor->y) - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize:
		wlr_cursor_warp_closest(
				cursor, NULL, grabc->geom.x + grabc->geom.width, grabc->geom.y + grabc->geom.height);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		cursorsync();
		break;
	}
}

static void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
		cursorsync();
	}
}

void
cursorsync(void)
{
	Monitor *m, *target;
	struct wlr_output_cursor *image;
	double rel_x, rel_y;
	double mm_x, mm_y;
	double scale_x, scale_y;

	if (!cursor)
		return;

	capture_update();
	m = xytomon(cursor->x, cursor->y);
	cursor_phys.mon = m;

	if (!m) {
		return;
	}

	cursor_phys.last_mon = m;
	rel_x = cursor->x - m->monitor_area.x;
	rel_y = cursor->y - m->monitor_area.y;

	scale_x = (m->phys.mm_per_px_x > 0) ? m->phys.mm_per_px_x : 1.0;
	scale_y = (m->phys.mm_per_px_y > 0) ? m->phys.mm_per_px_y : 1.0;
	mm_x = m->phys.origin_x_mm + rel_x * scale_x;
	mm_y = m->phys.origin_y_mm + rel_y * scale_y;

	cursor_phys.x_mm = mm_x;
	cursor_phys.y_mm = mm_y;

	/* wlroots places every output's cursor in desktop pixels. At a seam,
	 * that can show a fragment at the wrong height until the hotspot crosses.
	 * Project the physical hotspot onto each screen, even while it lies just
	 * outside that screen. Let wlroots clip the image at the output edge. */
	if (!cursor_config.physical_gap_jumps)
		return;
	wl_list_for_each(target, &mons, link) {
		if (!target->wlr_output->enabled || target->phys.mm_per_px_x <= 0 || target->phys.mm_per_px_y <= 0)
			continue;
		rel_x = (mm_x - target->phys.origin_x_mm) / target->phys.mm_per_px_x;
		rel_y = (mm_y - target->phys.origin_y_mm) / target->phys.mm_per_px_y;
		wl_list_for_each(image, &target->wlr_output->cursors, link) wlr_output_cursor_move(image, rel_x, rel_y);
	}
}

static void
cursorinteg(double dx, double dy)
{
	Monitor *basis;
	double scale_x = 1.0;
	double scale_y = 1.0;

	basis = cursor_phys.mon ? cursor_phys.mon : cursor_phys.last_mon;
	if (basis) {
		if (basis->phys.mm_per_px_x > 0)
			scale_x = basis->phys.mm_per_px_x;
		if (basis->phys.mm_per_px_y > 0)
			scale_y = basis->phys.mm_per_px_y;
		if (!cursor_phys.last_mon)
			cursor_phys.last_mon = basis;
	}

	cursor_phys.x_mm += dx * scale_x;
	cursor_phys.y_mm += dy * scale_y;
}

static int
cursorgap(Monitor *origin, double prev_mm_x, double prev_mm_y)
{
	Monitor *mon;
	double origin_left_mm, origin_right_mm, origin_top_mm, origin_bottom_mm;
	double anchor_mm;
	double best_distance = 1e18;
	Monitor *best = NULL;
	int dir = -1;
	const double tolerance_mm = 0.05;
	double target_left_mm = 0.0, target_right_mm = 0.0;
	double target_top_mm = 0.0, target_bottom_mm = 0.0;
	double scale_x = 1.0, scale_y = 1.0;
	double margin_x = 0.0, margin_y = 0.0;
	double new_mm_x, new_mm_y;
	double rel_mm_x, rel_mm_y;
	double target_x, target_y;
	double min_x, max_x, min_y, max_y;

	if (!origin || origin->phys.width_mm <= 0 || origin->phys.height_mm <= 0)
		return 0;

	origin_left_mm = origin->phys.origin_x_mm;
	origin_right_mm = origin_left_mm + origin->phys.width_mm;
	origin_top_mm = origin->phys.origin_y_mm;
	origin_bottom_mm = origin_top_mm + origin->phys.height_mm;

	/* Match the half-open pixel bounds: right/bottom already belong to
	 * the next screen, including moves that land exactly on the edge. */
	if (cursor_phys.x_mm < origin_left_mm)
		dir = WLR_DIRECTION_LEFT;
	else if (cursor_phys.x_mm >= origin_right_mm)
		dir = WLR_DIRECTION_RIGHT;
	else if (cursor_phys.y_mm < origin_top_mm)
		dir = WLR_DIRECTION_UP;
	else if (cursor_phys.y_mm >= origin_bottom_mm)
		dir = WLR_DIRECTION_DOWN;
	else
		return 0;

	if (prev_mm_x < origin_left_mm - tolerance_mm || prev_mm_x > origin_right_mm + tolerance_mm ||
			prev_mm_y < origin_top_mm - tolerance_mm || prev_mm_y > origin_bottom_mm + tolerance_mm)
		return 0;

	anchor_mm = (dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_RIGHT) ? cursor_phys.y_mm : cursor_phys.x_mm;

	wl_list_for_each(mon, &mons, link) {
		double cand_left_mm, cand_right_mm, cand_top_mm, cand_bottom_mm, distance;
		if (mon == origin || !mon->wlr_output || !mon->wlr_output->enabled)
			continue;
		if (mon->phys.width_mm <= 0 || mon->phys.height_mm <= 0)
			continue;

		cand_left_mm = mon->phys.origin_x_mm;
		cand_right_mm = cand_left_mm + mon->phys.width_mm;
		cand_top_mm = mon->phys.origin_y_mm;
		cand_bottom_mm = cand_top_mm + mon->phys.height_mm;

		switch (dir) {
		case WLR_DIRECTION_RIGHT:
			if (cand_left_mm < origin_right_mm)
				continue;
			if (anchor_mm < cand_top_mm - tolerance_mm || anchor_mm > cand_bottom_mm + tolerance_mm)
				continue;
			distance = cand_left_mm - origin_right_mm;
			break;
		case WLR_DIRECTION_LEFT:
			if (cand_right_mm > origin_left_mm)
				continue;
			if (anchor_mm < cand_top_mm - tolerance_mm || anchor_mm > cand_bottom_mm + tolerance_mm)
				continue;
			distance = origin_left_mm - cand_right_mm;
			break;
		case WLR_DIRECTION_DOWN:
			if (cand_top_mm < origin_bottom_mm)
				continue;
			if (anchor_mm < cand_left_mm - tolerance_mm || anchor_mm > cand_right_mm + tolerance_mm)
				continue;
			distance = cand_top_mm - origin_bottom_mm;
			break;
		case WLR_DIRECTION_UP:
			if (cand_bottom_mm > origin_top_mm)
				continue;
			if (anchor_mm < cand_left_mm - tolerance_mm || anchor_mm > cand_right_mm + tolerance_mm)
				continue;
			distance = origin_top_mm - cand_bottom_mm;
			break;
		default:
			continue;
		}

		if (distance < -tolerance_mm)
			continue;
		if (distance < best_distance) {
			best_distance = distance;
			best = mon;
		}
	}

	if (!best) {
		wlr_log(WLR_DEBUG, "cursor gap: no candidate from origin=%s",
				origin->wlr_output ? origin->wlr_output->name : "(null)");
		return 0;
	}

	target_left_mm = best->phys.origin_x_mm;
	target_right_mm = target_left_mm + best->phys.width_mm;
	target_top_mm = best->phys.origin_y_mm;
	target_bottom_mm = target_top_mm + best->phys.height_mm;
	scale_x = (best->phys.mm_per_px_x > 0) ? best->phys.mm_per_px_x : 1.0;
	scale_y = (best->phys.mm_per_px_y > 0) ? best->phys.mm_per_px_y : 1.0;
	margin_x = scale_x;
	margin_y = scale_y;
	new_mm_x = cursor_phys.x_mm;
	new_mm_y = cursor_phys.y_mm;

	if (margin_x > best->phys.width_mm / 2)
		margin_x = best->phys.width_mm / 2;
	if (margin_y > best->phys.height_mm / 2)
		margin_y = best->phys.height_mm / 2;

	if (dir == WLR_DIRECTION_RIGHT || dir == WLR_DIRECTION_LEFT) {
		if (new_mm_y < target_top_mm + margin_y)
			new_mm_y = target_top_mm + margin_y;
		if (new_mm_y > target_bottom_mm - margin_y)
			new_mm_y = target_bottom_mm - margin_y;
		if (dir == WLR_DIRECTION_RIGHT) {
			if (new_mm_x < target_left_mm + margin_x)
				new_mm_x = target_left_mm + margin_x;
			if (new_mm_x > target_right_mm - margin_x)
				new_mm_x = target_right_mm - margin_x;
		} else {
			if (new_mm_x > target_right_mm - margin_x)
				new_mm_x = target_right_mm - margin_x;
			if (new_mm_x < target_left_mm + margin_x)
				new_mm_x = target_left_mm + margin_x;
		}
	} else {
		if (new_mm_x < target_left_mm + margin_x)
			new_mm_x = target_left_mm + margin_x;
		if (new_mm_x > target_right_mm - margin_x)
			new_mm_x = target_right_mm - margin_x;
		if (dir == WLR_DIRECTION_DOWN) {
			if (new_mm_y < target_top_mm + margin_y)
				new_mm_y = target_top_mm + margin_y;
			if (new_mm_y > target_bottom_mm - margin_y)
				new_mm_y = target_bottom_mm - margin_y;
		} else {
			if (new_mm_y > target_bottom_mm - margin_y)
				new_mm_y = target_bottom_mm - margin_y;
			if (new_mm_y < target_top_mm + margin_y)
				new_mm_y = target_top_mm + margin_y;
		}
	}

	rel_mm_x = new_mm_x - target_left_mm;
	rel_mm_y = new_mm_y - target_top_mm;
	target_x = best->monitor_area.x + rel_mm_x / scale_x;
	target_y = best->monitor_area.y + rel_mm_y / scale_y;
	min_x = best->monitor_area.x;
	max_x = best->monitor_area.x + best->monitor_area.width - 1;
	min_y = best->monitor_area.y;
	max_y = best->monitor_area.y + best->monitor_area.height - 1;

	if (target_x < min_x)
		target_x = min_x;
	else if (target_x > max_x)
		target_x = max_x;
	if (target_y < min_y)
		target_y = min_y;
	else if (target_y > max_y)
		target_y = max_y;

	cursor_phys.x_mm = new_mm_x;
	cursor_phys.y_mm = new_mm_y;
	cursor_phys.mon = best;
	cursor_phys.last_mon = best;

	wlr_cursor_warp(cursor, NULL, target_x, target_y);
	cursorsync();
	motionnotify(0, NULL, 0, 0, 0, 0);
	return 1;
}

static void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy, uint32_t time)
{
	struct timespec now;
	/* focus follows mouse: update keyboard focus when hovering a different client */
	if (surface != seat->keyboard_state.focused_surface && cursor_config.sloppy_focus && time && c &&
			!client_is_unmanaged(c))
		focusclient(c, 0);
	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}
	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}
	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}
