/* Exercise physical placement independently of a display server. */
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "vwl.h"

struct wl_list mons;
static Monitor screens[6];
static struct wlr_output outputs[6];
static char *names[] = {"A", "B", "C", "D", "E", "F"};

static void
reset(size_t count)
{
	size_t i;
	wl_list_init(&mons);
	for (i = 0; i < count; i++) {
		outputs[i] = (struct wlr_output){.enabled = true, .name = names[i]};
		screens[i] = (Monitor){.wlr_output = &outputs[i]};
		wl_list_insert(mons.prev, &screens[i].link);
	}
}

static void
screen(size_t index, int x, int y, int width, int height, double mm_width, double mm_height)
{
	screens[index].monitor_area = (struct wlr_box){x, y, width, height};
	screens[index].phys = (MonitorPhysical){
			.width_mm = mm_width,
			.height_mm = mm_height,
			.mm_per_px_x = mm_width / width,
			.mm_per_px_y = mm_height / height,
	};
}

static void
fixed(size_t index, double x, double y)
{
	screens[index].phys.origin_configured = 1;
	screens[index].phys.origin_x_mm = x;
	screens[index].phys.origin_y_mm = y;
}

static void
expect(size_t index, double x, double y)
{
	assert(fabs(screens[index].phys.origin_x_mm - x) < 1e-6);
	assert(fabs(screens[index].phys.origin_y_mm - y) < 1e-6);
}

static void
no_overlap(size_t count)
{
	size_t i, j;
	for (i = 0; i < count; i++) {
		MonitorPhysical *a = &screens[i].phys;
		if (!outputs[i].enabled)
			continue;
		for (j = i + 1; j < count; j++) {
			MonitorPhysical *b = &screens[j].phys;
			if (!outputs[j].enabled || (a->origin_configured && b->origin_configured))
				continue;
			assert(a->origin_x_mm + a->width_mm <= b->origin_x_mm + 1e-6 ||
					b->origin_x_mm + b->width_mm <= a->origin_x_mm + 1e-6 ||
					a->origin_y_mm + a->height_mm <= b->origin_y_mm + 1e-6 ||
					b->origin_y_mm + b->height_mm <= a->origin_y_mm + 1e-6);
		}
	}
}

static void
stable(size_t count)
{
	double x[6], y[6];
	size_t i;
	for (i = 0; i < count; i++) {
		x[i] = screens[i].phys.origin_x_mm;
		y[i] = screens[i].phys.origin_y_mm;
	}
	/* Reverse discovery order, and discard all previous automatic results. */
	wl_list_init(&mons);
	for (i = 0; i < count; i++) {
		wl_list_insert(&mons, &screens[i].link);
		if (!screens[i].phys.origin_configured) {
			screens[i].phys.origin_x_mm = -12345;
			screens[i].phys.origin_y_mm = 12345;
		}
	}
	updatephysorigins();
	for (i = 0; i < count; i++) expect(i, x[i], y[i]);
}

int
main(void)
{
	size_t i, trial;
	reset(0);
	updatephysorigins();
	reset(2);
	screen(0, 0, 0, 1280, 720, 640, 360);
	screen(1, 1280, 0, 1280, 720, 320, 180);
	updatephysorigins();
	expect(0, 0, 0);
	expect(1, 640, 0);

	/* An explicit origin to the right must anchor the automatic left screen. */
	fixed(1, 500, 100);
	updatephysorigins();
	expect(0, -140, 100);
	expect(1, 500, 100);
	stable(2);
	/* Common translations of explicit origins translate the inferred ones. */
	fixed(1, 600, -100);
	updatephysorigins();
	expect(0, -40, -100);

	/* All four directions, negative desktop positions and an offset seam. */
	screen(0, -1280, 360, 1280, 720, 640, 360);
	screen(1, 0, 0, 1280, 720, 320, 180);
	fixed(1, 0, 0);
	updatephysorigins();
	expect(0, -640, 90);
	screen(0, 1280, 360, 1280, 720, 640, 360);
	updatephysorigins();
	expect(0, 320, 90);
	screen(0, 640, -720, 1280, 720, 640, 360);
	updatephysorigins();
	expect(0, 160, -360);
	screen(0, 640, 720, 1280, 720, 640, 360);
	updatephysorigins();
	expect(0, 160, 180);

	/* Preserve bottom-aligned and right-aligned desktop edges. */
	screen(0, 1280, 360, 1280, 360, 320, 90);
	updatephysorigins();
	expect(0, 320, 90);
	screen(0, 640, 720, 640, 720, 160, 180);
	updatephysorigins();
	expect(0, 160, 180);
	/* A desktop gap uses the arriving screen's density. */
	screen(0, 1380, 0, 1280, 720, 640, 360);
	updatephysorigins();
	expect(0, 370, 0);

	/* The bottom-right screen must not fall inside the larger top-left one. */
	reset(4);
	for (i = 0; i < 4; i++) screen(i, (i % 2) * 1280, (i / 2) * 720, 1280, 720, 320, 180);
	screen(0, 0, 0, 1280, 720, 640, 360);
	updatephysorigins();
	expect(0, 0, 0);
	expect(1, 640, 0);
	expect(2, 0, 360);
	/* Different neighbour choices can give valid layouts; require that the
	 * inferred screen stays outside every other screen. */
	no_overlap(4);
	stable(4);

	/* Keep both explicit screens fixed when an automatic screen has to fit
	 * between them. If it cannot fit, slide it out of their way. */
	fixed(0, 0, 0);
	fixed(1, 200, 400);
	updatephysorigins();
	expect(0, 0, 0);
	expect(1, 200, 400);
	no_overlap(4);
	stable(4);
	outputs[1].enabled = false;
	updatephysorigins();
	no_overlap(4);
	outputs[1].enabled = true;
	updatephysorigins();
	expect(1, 200, 400);
	no_overlap(4);

	/* Overlapping explicit positions are intentional; leave them alone and
	 * keep automatic screens outside both rectangles. */
	fixed(1, 200, 100);
	updatephysorigins();
	expect(0, 0, 0);
	expect(1, 200, 100);
	no_overlap(4);
	stable(4);

	/* Deterministic mixed-density grids, with varying explicit anchors.
	 * Check invariants instead of reproducing the placement algorithm. */
	for (trial = 0; trial < 80; trial++) {
		reset(6);
		for (i = 0; i < 6; i++) {
			screen(i, ((int)(i % 3) - 1) * 1280, ((int)(i / 3) - 1) * 720, 1280, 720,
					200.125 + ((trial * 71 + i * 113) % 500),
					120.3 + ((trial * 31 + i * 67) % 400));
		}
		fixed(trial % 6, -100.1, 200.2);
		updatephysorigins();
		expect(trial % 6, -100.1, 200.2);
		no_overlap(6);
		stable(6);
	}
	puts("physical: anchors, mixed densities, grids, gaps and discovery order passed");
	return 0;
}
