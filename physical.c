/* Infer physical screen positions without overlapping automatic screens.
 * Desktop pixels cannot describe every mixed-density arrangement exactly.
 * Keep explicit origins fixed and attach each automatic screen to one already
 * placed neighbour, moving it only as far as needed to avoid other screens. */
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "vwl.h"
#include "util.h"

enum { PhysLeft, PhysRight, PhysUp, PhysDown };
struct placement {
	Monitor *mon;
	bool placed;
};

static int
screen_order(const void *a, const void *b)
{
	const Monitor *m = ((const struct placement *)a)->mon;
	const Monitor *n = ((const struct placement *)b)->mon;
	if (m->monitor_area.x != n->monitor_area.x)
		return m->monitor_area.x < n->monitor_area.x ? -1 : 1;
	if (m->monitor_area.y != n->monitor_area.y)
		return m->monitor_area.y < n->monitor_area.y ? -1 : 1;
	return strcmp(m->wlr_output->name, n->wlr_output->name);
}

static bool
fits(struct placement *screens, size_t count, Monitor *m, double x, double y)
{
	size_t i;
	const double epsilon = 1e-7; /* Ignore round-off at touching edges. */
	for (i = 0; i < count; i++) {
		MonitorPhysical *p = &screens[i].mon->phys;
		if (screens[i].placed && x < p->origin_x_mm + p->width_mm - epsilon &&
				x + m->phys.width_mm > p->origin_x_mm + epsilon &&
				y < p->origin_y_mm + p->height_mm - epsilon &&
				y + m->phys.height_mm > p->origin_y_mm + epsilon)
			return false;
	}
	return true;
}

/* A nearest free rectangle has each coordinate either at the preferred
 * position or against an obstacle edge. Check those finite candidates while
 * keeping the screen on the chosen side of its neighbour. */
static double
find_space(struct placement *screens, size_t count, Monitor *m, Monitor *anchor, int dir, double *x, double *y,
		double *xs, double *ys)
{
	size_t i, j, nx = 1, ny = 1;
	double best = DBL_MAX, best_x = *x, best_y = *y, distance;
	const double epsilon = 1e-7;
	MonitorPhysical *a = &anchor->phys;
	xs[0] = *x;
	ys[0] = *y;
	for (i = 0; i < count; i++) {
		MonitorPhysical *p = &screens[i].mon->phys;
		if (!screens[i].placed)
			continue;
		xs[nx++] = p->origin_x_mm - m->phys.width_mm;
		xs[nx++] = p->origin_x_mm + p->width_mm;
		ys[ny++] = p->origin_y_mm - m->phys.height_mm;
		ys[ny++] = p->origin_y_mm + p->height_mm;
	}
	for (i = 0; i < nx; i++) {
		for (j = 0; j < ny; j++) {
			if ((dir == PhysLeft && xs[i] + m->phys.width_mm > a->origin_x_mm + epsilon) ||
					(dir == PhysRight && xs[i] < a->origin_x_mm + a->width_mm - epsilon) ||
					(dir == PhysUp && ys[j] + m->phys.height_mm > a->origin_y_mm + epsilon) ||
					(dir == PhysDown && ys[j] < a->origin_y_mm + a->height_mm - epsilon))
				continue;
			distance = (xs[i] - *x) * (xs[i] - *x) + (ys[j] - *y) * (ys[j] - *y);
			if (distance < best && fits(screens, count, m, xs[i], ys[j])) {
				best = distance;
				best_x = xs[i];
				best_y = ys[j];
			}
		}
	}
	*x = best_x;
	*y = best_y;
	return best;
}

/* Convert the along-edge offset using the neighbour's density. Honour exact
 * bottom/right alignment as well as the usual top/left alignment. */
static double
edge_offset(int start, int size, int anchor_start, int anchor_size, double mm, double anchor_mm, double scale)
{
	if (start != anchor_start && (double)start + size == (double)anchor_start + anchor_size)
		return anchor_mm - mm;
	return ((double)start - anchor_start) * scale;
}

static bool
propose(Monitor *m, Monitor *n, int dir, double *x, double *y)
{
	struct wlr_box *b = &m->monitor_area, *a = &n->monitor_area;
	double left = a->x, right = left + a->width, top = a->y, bottom = top + a->height;
	double mr = (double)b->x + b->width, mb = (double)b->y + b->height;
	bool overlap = b->x < right && mr > left && b->y < bottom && mb > top;
	*x = n->phys.origin_x_mm;
	*y = n->phys.origin_y_mm;
	if (dir == PhysLeft || dir == PhysRight) {
		*y += edge_offset(b->y, b->height, a->y, a->height, m->phys.height_mm, n->phys.height_mm,
				n->phys.mm_per_px_y);
		if (dir == PhysLeft && mr <= left)
			*x -= m->phys.width_mm + (left - mr) * m->phys.mm_per_px_x;
		else if (dir == PhysRight && (b->x >= right || overlap))
			*x += n->phys.width_mm + MAX(0, b->x - right) * m->phys.mm_per_px_x;
		else
			return false;
	} else {
		*x += edge_offset(b->x, b->width, a->x, a->width, m->phys.width_mm, n->phys.width_mm,
				n->phys.mm_per_px_x);
		if (dir == PhysUp && mb <= top)
			*y -= m->phys.height_mm + (top - mb) * m->phys.mm_per_px_y;
		else if (dir == PhysDown && b->y >= bottom)
			*y += n->phys.height_mm + (b->y - bottom) * m->phys.mm_per_px_y;
		else
			return false;
	}
	return true;
}

void
updatephysorigins(void)
{
	Monitor *m;
	struct placement *screens;
	size_t count = 0, placed = 0, i, j, best_index = 0;
	int dir;
	double *xs, *ys;

	if (wl_list_empty(&mons))
		return;
	screens = ecalloc(wl_list_length(&mons), sizeof(*screens));
	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled || m->monitor_area.width <= 0 || m->monitor_area.height <= 0 ||
				m->phys.width_mm <= 0 || m->phys.height_mm <= 0)
			continue;
		screens[count++] = (struct placement){m, m->phys.origin_configured};
		placed += !!m->phys.origin_configured;
	}
	qsort(screens, count, sizeof(*screens), screen_order);
	if (count && !placed) {
		m = screens[0].mon;
		m->phys.origin_x_mm = m->monitor_area.x * m->phys.mm_per_px_x;
		m->phys.origin_y_mm = m->monitor_area.y * m->phys.mm_per_px_y;
		screens[0].placed = true;
		placed = 1;
	}
	xs = ecalloc(2 * count + 1, sizeof(*xs));
	ys = ecalloc(2 * count + 1, sizeof(*ys));
	while (placed < count) {
		double best_gap = DBL_MAX, best_shift = DBL_MAX, best_x = 0, best_y = 0;
		int best_shared = -1;
		for (i = 0; i < count; i++) {
			struct wlr_box *b = &screens[i].mon->monitor_area;
			if (screens[i].placed)
				continue;
			for (j = 0; j < count; j++) {
				struct wlr_box *a = &screens[j].mon->monitor_area;
				double dx, dy, gap, x, y, shift;
				int shared;
				if (!screens[j].placed)
					continue;
				dx = MAX(0, MAX((double)b->x - a->x - a->width, (double)a->x - b->x - b->width));
				dy = MAX(0, MAX((double)b->y - a->y - a->height, (double)a->y - b->y - b->height));
				gap = dx * dx + dy * dy;
				shared = (b->x < (double)a->x + a->width && a->x < (double)b->x + b->width) ||
					 (b->y < (double)a->y + a->height && a->y < (double)b->y + b->height);
				for (dir = PhysLeft; dir <= PhysDown; dir++) {
					if (!propose(screens[i].mon, screens[j].mon, dir, &x, &y))
						continue;
					shift = find_space(screens, count, screens[i].mon, screens[j].mon, dir, &x, &y,
							xs, ys);
					if (shift == DBL_MAX || gap > best_gap)
						continue;
					if (gap == best_gap && shared < best_shared)
						continue;
					if (gap == best_gap && shared == best_shared && shift >= best_shift)
						continue;
					best_gap = gap;
					best_shared = shared;
					best_shift = shift;
					best_index = i;
					best_x = x;
					best_y = y;
				}
			}
		}
		m = screens[best_index].mon;
		m->phys.origin_x_mm = best_x;
		m->phys.origin_y_mm = best_y;
		screens[best_index].placed = true;
		placed++;
	}
	free(ys);
	free(xs);
	free(screens);
}
