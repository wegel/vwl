/* Window layouts and tab order. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "vwl.h"
#include "ipc.h"
#include "share.h"
#include "tabhdr.h"

void
tabbed(Monitor *m)
{
	VirtualOutput *vout = focusedvout(m);
	struct wlr_box area;
	struct wlr_box client_box;
	Client *c, *active;
	int count = 0;

	if (!vout)
		return;
	area = voutarea(vout);
	active = focustoptiledvout(vout);
	client_box = tabhdr_client_area(area);

	wl_list_for_each(c, &clients, link) {
		bool virtual_fs;

		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, m) || c->isfloating)
			continue;
		virtual_fs = client_is_virtual_fullscreen(c);
		if (!virtual_fs && c->isfullscreen)
			continue;
		count++;
		if (c == active) {
			if (!virtual_fs && client_box.height > 0 &&
					(c->geom.x != client_box.x || c->geom.y != client_box.y ||
							c->geom.width != client_box.width ||
							c->geom.height != client_box.height))
				resize(c, client_box, 0);
			wlr_scene_node_set_enabled(&c->scene->node, 1);
			client_set_suspended(c, 0);
		} else {
			wlr_scene_node_set_enabled(&c->scene->node, 0);
			client_set_suspended(c, !share_is_captured(c));
		}
	}

	if (count > 1)
		snprintf(vout->ltsymbol, LENGTH(vout->ltsymbol), "[T:%d]", count);

	if (active)
		wlr_scene_node_raise_to_top(&active->scene->node);

	tabhdr_update(m, vout, area, active);
}

void
tile(Monitor *m)
{
	unsigned int mw, my, ty;
	int i, n = 0;
	Client *c;
	VirtualOutput *vout = focusedvout(m);
	struct wlr_box area;

	if (!vout)
		return;
	area = voutarea(vout);

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
			resize(c,
					(struct wlr_box){.x = area.x,
							.y = area.y + my,
							.width = mw,
							.height = (area.height - my) / (MIN(n, vout->nmaster) - i)},
					0);
			my += c->geom.height;
		} else {
			resize(c,
					(struct wlr_box){.x = area.x + mw,
							.y = area.y + ty,
							.width = area.width - mw,
							.height = (area.height - ty) / (n - i)},
					0);
			ty += c->geom.height;
		}
		i++;
	}
}

void
tabmove(const Arg *arg)
{
	Client *sel = focustop(selmon);
	VirtualOutput *vout = focusedvout(selmon);
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

	for (link = dir > 0 ? sel->link.next : sel->link.prev; link != &clients;
			link = dir > 0 ? link->next : link->prev) {
		Client *c = wl_container_of(link, sel, link);
		if (CLIENT_VOUT(c) != vout || !VISIBLEON(c, selmon) || c->isfloating ||
				client_is_nonvirtual_fullscreen(c))
			continue;
		target = c;
		break;
	}
	if (!target)
		return;
	wl_list_remove(&sel->link);
	wl_list_insert(dir > 0 ? &target->link : target->link.prev, &sel->link);

	{
		struct wlr_box area;
		area = voutarea(vout);
		tabhdr_update(selmon, vout, area, sel);
	}
	focusclient(sel, 1);
}

void
setlayout(const Arg *arg)
{
	VirtualOutput *vout = focusedvout(selmon);

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

/* arg > 1.0 sets mfact absolutely. */
void
setmfact(const Arg *arg)
{
	float f;
	VirtualOutput *vout = focusedvout(selmon);

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
toggletabbed(const Arg *arg)
{
	const Layout *tab = arg && arg->v ? arg->v : NULL;
	Arg tile_arg = {.v = &workspace_config.layouts[0]};
	VirtualOutput *vout = focusedvout(selmon);
	if (!vout || !tab)
		return;
	if (vout->lt[vout->sellt] == tab)
		setlayout(&tile_arg);
	else
		setlayout(&(Arg){.v = tab});
}

Client *
focustoptiledvout(VirtualOutput *vout)
{
	Client *c;

	if (!vout)
		return NULL;
	wl_list_for_each(c, &fstack, flink) {
		if (c->ws == vout->ws && !client_is_unmanaged(c) && !c->isfloating &&
				!client_is_nonvirtual_fullscreen(c))
			return c;
	}
	wl_list_for_each(c, &clients, link) {
		if (c->ws == vout->ws && !client_is_unmanaged(c) && !c->isfloating &&
				!client_is_nonvirtual_fullscreen(c))
			return c;
	}
	return NULL;
}
