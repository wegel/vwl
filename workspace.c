/* Workspaces and the virtual outputs that display them. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include "vwl.h"
#include "ipc.h"
#include "util.h"

Workspace workspaces[WORKSPACE_COUNT];
Workspace *selws;
VirtualOutput *selvout;

static void wsinsert(VirtualOutput *vout, Workspace *ws);
static void wsload(VirtualOutput *vout, Workspace *ws);
static Workspace *wsnext(VirtualOutput *vout, Workspace *exclude);
static VirtualOutput *voutonmonitor(Monitor *mon, enum wlr_direction dir, struct wlr_box ref, int require_overlap);
static VirtualOutput *findvoutindir(VirtualOutput *from, enum wlr_direction dir);

Workspace *
wsbyid(unsigned int id)
{
	return id < WORKSPACE_COUNT ? &workspaces[id] : NULL;
}

VirtualOutput *
firstvout(Monitor *m)
{
	VirtualOutput *vout;
	if (!m || wl_list_empty(&m->vouts)) {
		return NULL;
	}
	vout = wl_container_of(m->vouts.next, vout, link);
	return vout;
}

VirtualOutput *
voutbyid(unsigned int id)
{
	Monitor *m;
	VirtualOutput *vout;

	wl_list_for_each(m, &mons, link) {
		wl_list_for_each(vout, &m->vouts, link) {
			if (vout->id == id)
				return vout;
		}
	}

	return NULL;
}

VirtualOutput *
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

int
ipc_set_workspace_by_id(unsigned int workspace_id)
{
	if (!wsbyid(workspace_id))
		return -1;

	view(&(Arg){.ui = workspace_id});
	return 0;
}

int
ipc_focus_virtual_output(VirtualOutput *vout)
{
	if (!vout || !vout->mon)
		return -1;

	selmon = vout->mon;
	selmon->focus_vout = vout;
	selvout = vout;
	selws = vout->ws;
	if (vout->ws)
		wsactivate(vout, vout->ws, 1);
	focusclient(focustopvout(vout), 1);
	cursorwarptovout(vout);
	updateipc();
	return 0;
}

int
ipc_move_workspace_to_vout(Workspace *ws, VirtualOutput *vout)
{
	if (!ws || !vout || !vout->mon)
		return -1;

	wsmoveto(ws, vout);

	selmon = vout->mon;
	selmon->focus_vout = vout;
	selvout = vout;
	selws = ws;
	if (vout->ws != ws)
		wsactivate(vout, ws, 1);
	focusclient(focustopvout(vout), 1);
	cursorwarptovout(vout);
	updateipc();
	return 0;
}

VirtualOutput *
focusedvout(Monitor *m)
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
	return focusedvout(m);
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
		geom.x = base.x + vout->rule_geom.x;
		geom.y = base.y + vout->rule_geom.y;
		geom.width = vout->rule_geom.width > 0 ? vout->rule_geom.width : base.width - vout->rule_geom.x;
		geom.height = vout->rule_geom.height > 0 ? vout->rule_geom.height : base.height - vout->rule_geom.y;

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

VirtualOutput *
createvout(Monitor *m, const char *name)
{
	static unsigned int next_id = 1;
	VirtualOutput *vout;
	const VirtualOutputRule *default_rule = NULL;
	size_t j;

	if (!m)
		return NULL;

	/* find the default (fallback) vout rule */
	for (j = 0; j < workspace_config.rule_count; j++) {
		if (!workspace_config.rules[j].monitor) {
			default_rule = &workspace_config.rules[j];
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
	vout->lt[0] = &workspace_config.layouts[0];
	vout->lt[1] = (workspace_config.layout_count > 1) ? &workspace_config.layouts[1] : vout->lt[0];
	strncpy(vout->name, name ? name : m->wlr_output->name, sizeof(vout->name) - 1);
	vout->name[sizeof(vout->name) - 1] = '\0';
	strncpy(vout->ltsymbol, vout->lt[vout->sellt]->symbol, LENGTH(vout->ltsymbol));
	vout->ltsymbol[LENGTH(vout->ltsymbol) - 1] = '\0';
	if (wl_list_empty(&m->vouts))
		wl_list_insert(&m->vouts, &vout->link);
	else
		wl_list_insert(m->vouts.prev, &vout->link);
	wl_list_init(&vout->workspaces);
	wl_signal_init(&vout->events.destroy);
	vout->tabhdr = wlr_scene_tree_create(layers[LyrTop]);
	if (vout->tabhdr)
		wlr_scene_node_set_enabled(&vout->tabhdr->node, 0);
	if (!m->focus_vout)
		m->focus_vout = vout;
	return vout;
}

void
destroyvout(VirtualOutput *vout)
{
	Workspace *ws, *tmp;
	Monitor *m;
	if (!vout)
		return;
	m = vout->mon;
	if (vout->ws)
		wssave(vout);
	wl_signal_emit_mutable(&vout->events.destroy, vout);
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
	if (selvout == vout)
		selvout = NULL;
	if (hover.vout == vout)
		hover.vout = NULL;
	free(vout);
}

Workspace *
wsfindfree(void)
{
	unsigned int i;

	for (i = 1; i < MIN(WORKSPACE_COUNT, 10); i++)
		if (!workspaces[i].vout && !workspaces[i].was_orphaned)
			return &workspaces[i];
	if (WORKSPACE_COUNT > 0 && !workspaces[0].vout && !workspaces[0].was_orphaned)
		return &workspaces[0];
	for (i = 10; i < WORKSPACE_COUNT; i++)
		if (!workspaces[i].vout && !workspaces[i].was_orphaned)
			return &workspaces[i];
	return NULL;
}

void
attachvoutworkspaces(VirtualOutput *vout, const unsigned int *workspace_ids, size_t workspace_count)
{
	size_t i;

	if (!vout || !workspace_ids)
		return;

	for (i = 0; i < workspace_count; i++) {
		Workspace *ws;

		if (workspace_ids[i] >= WORKSPACE_COUNT)
			continue;
		ws = &workspaces[workspace_ids[i]];
		/* Defaults apply only to workspaces that have no current or saved home. */
		if (!ws->vout && !ws->was_orphaned)
			wsattach(vout, ws);
	}
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

void
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
	vout->lt[0] = ws->state.lt[0] ? ws->state.lt[0] : &workspace_config.layouts[0];
	vout->lt[1] = ws->state.lt[1] ? ws->state.lt[1] : vout->lt[0];
	if (vout->sellt > 1)
		vout->sellt = 0;
	strncpy(vout->ltsymbol, vout->lt[vout->sellt]->symbol, LENGTH(vout->ltsymbol));
	vout->ltsymbol[LENGTH(vout->ltsymbol) - 1] = '\0';
}

Workspace *
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

void
wsattach(VirtualOutput *vout, Workspace *ws)
{
	VirtualOutput *old;
	Client *c;
	if (!vout || !ws)
		return;
	if (ws->vout == vout)
		return;
	old = ws->vout;
	if (old) {
		if (old->ws == ws) {
			wssave(old);
			old->ws = NULL;
		}
		wl_list_remove(&ws->link);
	}
	ws->vout = vout;
	wsinsert(vout, ws);
	/* Every path that moves a workspace must move its windows with it. */
	wl_list_for_each(c, &clients, link) {
		if (c->ws == ws)
			setworkspace(c, ws);
	}
	if (old && !old->ws)
		wsactivate(old, wsfirst(old), 0);
}

void
wsactivate(VirtualOutput *vout, Workspace *ws, int focus_change)
{
	Workspace *old;
	Monitor *m;
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
	if (ws)
		wsload(vout, ws);
	else
		vout->ws = NULL;
	if (m == selmon) {
		selws = ws;
		selvout = vout;
	}
	arrange(m);
	if (focus_change)
		focusclient(focustopvout(vout), 1);
}

void
wsmoveto(Workspace *ws, VirtualOutput *vout)
{
	VirtualOutput *old;
	Workspace *fallback;
	Monitor *old_mon;
	if (!ws || !vout)
		return;
	/* An explicit move replaces the home saved when a monitor disappeared. */
	ws->was_orphaned = false;
	ws->orphan_was_active = false;
	ws->orphan_vout_name[0] = '\0';
	ws->orphan_monitor_name[0] = '\0';
	if (ws->vout == vout)
		return;
	old = ws->vout;
	old_mon = old ? old->mon : NULL;
	fallback = old ? old->ws : NULL;
	wsattach(vout, ws);
	wsactivate(vout, ws, 1);
	if (old) {
		if (fallback && fallback != ws)
			return;
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
		if (!cursor_config.warp_to_vout && selvout) {
			vout = selvout;
		} else if (cursor) {
			Monitor *pointer_mon = xytomon(cursor->x, cursor->y);
			VirtualOutput *pointer_vout = pointer_mon ? voutat(pointer_mon, cursor->x, cursor->y) : NULL;
			if (pointer_vout)
				target = pointer_vout;
			else if (pointer_mon)
				target = focusedvout(pointer_mon);
		}
		if (!target && selmon)
			target = focusedvout(selmon);
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
	origin_vout = active->vout ? active->vout : focusedvout(selmon);
	target_vout = findvoutindir(origin_vout, arg->i);
	if (!target_vout)
		return;
	target = target_vout->mon;

	focused = origin_vout ? focustopvout(origin_vout) : focustop(selmon);
	warp_needed = target_vout != origin_vout;
	wsmoveto(active, target_vout);
	selmon = target;
	selvout = target_vout;
	selws = target_vout->ws;
	if (selmon)
		selmon->focus_vout = target_vout;
	if (focused)
		focusclient(focused, 1);
	else
		focusclient(focustop(selmon), 1);
	if (warp_needed)
		cursorwarptovout(target_vout);
	updateipc();
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
		VirtualOutput *vout = focusedvout(selmon);
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
		VirtualOutput *vout = focusedvout(m);
		if (vout && vout->ws)
			setworkspace(sel, vout->ws);
	}
}

static VirtualOutput *
voutonmonitor(Monitor *mon, enum wlr_direction dir, struct wlr_box ref, int require_overlap)
{
	VirtualOutput *best = NULL, *vout;
	double ref_cx, ref_cy;
	double best_metric = (dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_UP) ? -DBL_MAX : DBL_MAX;
	double best_ortho = DBL_MAX;

	/* Diagonal movements are not supported */
	if (dir & (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT) && dir & (WLR_DIRECTION_UP | WLR_DIRECTION_DOWN))
		return NULL;

	ref_cx = ref.x + ref.width / 2.0;
	ref_cy = ref.y + ref.height / 2.0;

	wl_list_for_each(vout, &mon->vouts, link) {
		struct wlr_box box;
		double cx, cy;
		bool overlaps = false;

		box = voutarea(vout);
		if (wlr_box_empty(&box))
			continue;

		if (dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_RIGHT)
			overlaps = box.y < ref.y + ref.height && ref.y < box.y + box.height;
		else
			overlaps = box.x < ref.x + ref.width && ref.x < box.x + box.width;
		if (require_overlap && !overlaps)
			continue;

		cx = box.x + box.width / 2.0;
		cy = box.y + box.height / 2.0;

		if (dir & WLR_DIRECTION_LEFT) {
			double ortho = fabs(cy - ref_cy);
			if (cx >= ref_cx ||
					(!require_overlap &&
							(ortho > best_ortho ||
									(ortho == best_ortho && cx <= best_metric))) ||
					(require_overlap && cx <= best_metric)) {
				continue;
			}
			best_metric = cx, best_ortho = ortho, best = vout;
		} else if (dir & WLR_DIRECTION_RIGHT) {
			double ortho = fabs(cy - ref_cy);
			if (cx <= ref_cx ||
					(!require_overlap &&
							(ortho > best_ortho ||
									(ortho == best_ortho && cx >= best_metric))) ||
					(require_overlap && cx >= best_metric)) {
				continue;
			}
			best_metric = cx, best_ortho = ortho, best = vout;
		} else if (dir & WLR_DIRECTION_UP) {
			double ortho = fabs(cx - ref_cx);
			if (cy >= ref_cy ||
					(!require_overlap &&
							(ortho > best_ortho ||
									(ortho == best_ortho && cy <= best_metric))) ||
					(require_overlap && cy <= best_metric)) {
				continue;
			}
			best_metric = cy, best_ortho = ortho, best = vout;
		} else if (dir & WLR_DIRECTION_DOWN) {
			double ortho = fabs(cx - ref_cx);
			if (cy <= ref_cy ||
					(!require_overlap &&
							(ortho > best_ortho ||
									(ortho == best_ortho && cy >= best_metric))) ||
					(require_overlap && cy >= best_metric)) {
				continue;
			}
			best_metric = cy, best_ortho = ortho, best = vout;
		}
	}
	return best;
}

static VirtualOutput *
findvoutindir(VirtualOutput *from, enum wlr_direction dir)
{
	struct wlr_box ref;
	VirtualOutput *vout;
	Monitor *target_mon;
	struct wlr_box target_ref;

	if (!from || !from->mon || !dir)
		return NULL;

	ref = voutarea(from);
	if (wlr_box_empty(&ref))
		return NULL;

	vout = voutonmonitor(from->mon, dir, ref, 1);
	if (vout && vout != from)
		return vout;
	target_mon = dirtomonfrom(from->mon, dir);
	if (!target_mon)
		return NULL;
	target_ref = !wlr_box_empty(&target_mon->window_area) ? target_mon->window_area : target_mon->monitor_area;
	if (wlr_box_empty(&target_ref))
		return NULL;
	if (dir & WLR_DIRECTION_LEFT)
		ref.x = target_ref.x + target_ref.width;
	else if (dir & WLR_DIRECTION_RIGHT)
		ref.x = target_ref.x - ref.width;
	else if (dir & WLR_DIRECTION_UP)
		ref.y = target_ref.y + target_ref.height;
	else if (dir & WLR_DIRECTION_DOWN)
		ref.y = target_ref.y - ref.height;
	vout = voutonmonitor(target_mon, dir, ref, 1);
	if (!vout)
		vout = voutonmonitor(target_mon, dir, ref, 0);
	return vout == from ? NULL : vout;
}

void
focusvout(const Arg *arg)
{
	VirtualOutput *target_vout;

	if (!selvout || !arg || !arg->i)
		return;

	target_vout = findvoutindir(selvout, arg->i);

	if (!target_vout || target_vout == selvout)
		return;

	ipc_focus_virtual_output(target_vout);
}

struct wlr_box
voutarea(VirtualOutput *vout)
{
	return !wlr_box_empty(&vout->layout_geom) ? vout->layout_geom : vout->mon->window_area;
}

void
workspaces_init(void)
{
	const VirtualOutputRule *defvorule = NULL;
	size_t j;
	unsigned int i;

	for (j = 0; j < workspace_config.rule_count; j++) {
		if (!workspace_config.rules[j].monitor) {
			defvorule = &workspace_config.rules[j];
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
		ws->state.lt[0] = defvorule && defvorule->lt_primary ? defvorule->lt_primary
								     : &workspace_config.layouts[0];
		ws->state.lt[1] = defvorule && defvorule->lt_secondary	? defvorule->lt_secondary
				  : (workspace_config.layout_count > 1) ? &workspace_config.layouts[1]
									: ws->state.lt[0];
		ws->was_orphaned = false;
		ws->orphan_was_active = false;
		ws->orphan_vout_name[0] = '\0';
		ws->orphan_monitor_name[0] = '\0';
	}
	selws = NULL;
}
