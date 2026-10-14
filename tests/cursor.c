/* Check cursor fragments on both sides of a physically arranged screen seam. */
#include "compositor.h"

static struct wlr_output_cursor *
image(Monitor *m)
{
	struct wlr_output_cursor *result;
	assert(wl_list_length(&m->wlr_output->cursors) == 1);
	result = wl_container_of(m->wlr_output->cursors.next, result, link);
	return result;
}

static void
expect_image(Monitor *m, double x, double y)
{
	struct wlr_output_cursor *i = image(m);
	assert(fabs(i->x - x * m->wlr_output->scale) < 0.001);
	assert(fabs(i->y - y * m->wlr_output->scale) < 0.001);
}

static void
place(Monitor *m, int x, int y, double mm_x, double mm_y, double width_mm, double height_mm)
{
	m->phys = (MonitorPhysical){
			.width_mm = width_mm,
			.height_mm = height_mm,
			.origin_x_mm = mm_x,
			.origin_y_mm = mm_y,
			.width_configured = 1,
			.height_configured = 1,
			.origin_configured = 1,
	};
	assert(wlr_output_layout_add(output_layout, m->wlr_output, x, y));
	updatephys(m);
	cursorsync();
}

static void
warp(double x, double y)
{
	assert(wlr_cursor_warp(cursor, NULL, x, y));
	cursorsync();
}

int
main(void)
{
	char runtime[] = "/tmp/vwl-cursor-XXXXXX";
	Monitor *left, *right;
	struct wlr_output_state state;
	struct wlr_output_cursor *i;
	double before_y;

	test_setup(runtime, "0", NULL);
	left = add_monitor("CURSOR-LEFT");
	right = add_monitor("CURSOR-RIGHT");
	assert(left && right);
	place(left, 0, 0, 0, 0, 640, 360);
	place(right, 1280, 0, 640, 100, 320, 180);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	/* Before vwl corrects it, wlroots puts the neighbouring fragment at
	 * y=300. The physical height on the smaller, lower screen is y=200. */
	assert(wlr_cursor_warp(cursor, NULL, 1279, 300));
	expect_image(right, -1, 300);
	cursorsync();
	expect_image(left, 1279, 300);
	expect_image(right, -2, 200);
	assert(image(right)->visible);
	before_y = image(right)->y;
	motionnotify(1, NULL, 4, 0, 4, 0);
	assert(cursor_phys.mon == right);
	expect_image(right, 6, 200);
	assert(image(right)->y == before_y);
	expect_image(left, 1283, 300);

	/* The same physical height must survive the return crossing. */
	warp(1281, 200);
	expect_image(left, 1280.5, 300);
	motionnotify(2, NULL, -4, 0, -4, 0);
	assert(cursor_phys.mon == left);
	expect_image(left, 1278.5, 300);
	expect_image(right, -3, 200);

	/* Exact right/bottom edges and tiny left/up excursions must not fall
	 * through to wlroots' unrelated desktop coordinates. */
	warp(1279, 300);
	motionnotify(7, NULL, 1, 0, 1, 0);
	assert(cursor_phys.mon == right);
	expect_image(right, 1, 200);
	warp(1280, 200);
	motionnotify(8, NULL, -0.01, 0, -0.01, 0);
	assert(cursor_phys.mon == left);
	expect_image(left, 1279, 300);

	/* A monitor below the first uses the physical horizontal offset. */
	place(right, 0, 720, 100, 360, 320, 180);
	warp(300, 719);
	expect_image(right, 200, -2);
	assert(image(right)->visible);
	motionnotify(3, NULL, 0, 4, 0, 4);
	assert(cursor_phys.mon == right);
	expect_image(right, 200, 6);
	expect_image(left, 300, 723);
	warp(200, 721);
	motionnotify(4, NULL, 0, -4, 0, -4);
	assert(cursor_phys.mon == left);
	expect_image(left, 300, 718.5);

	warp(300, 719);
	motionnotify(9, NULL, 0, 1, 0, 1);
	assert(cursor_phys.mon == right);
	expect_image(right, 200, 1);
	warp(200, 720);
	motionnotify(10, NULL, 0, -0.01, 0, -0.01);
	assert(cursor_phys.mon == left);
	expect_image(left, 300, 719);

	/* Desktop neighbours separated by a real gap must not show a false
	 * fragment. The existing gap jump still lands on the correct screen. */
	place(right, 1280, 0, 660, 100, 320, 180);
	warp(1279, 300);
	assert(!image(right)->visible);
	motionnotify(5, NULL, 4, 0, 4, 0);
	assert(cursor_phys.mon == right);
	expect_image(right, 1, 200);
	assert(!image(left)->visible);

	/* Auto origins chain through each neighbour's own physical width. */
	left->phys.origin_configured = 0;
	right->phys.origin_configured = 0;
	updatephysorigins();
	assert(left->phys.origin_x_mm == 0);
	assert(right->phys.origin_x_mm == 640);
	assert(right->phys.origin_y_mm == 0);

	/* A partially shared edge offsets in the neighbour's density. */
	place(right, 1280, 360, 0, 0, 320, 180);
	right->phys.origin_configured = 0;
	updatephysorigins();
	assert(right->phys.origin_x_mm == 640);
	assert(right->phys.origin_y_mm == 180);
	place(right, 640, 720, 0, 0, 320, 180);
	right->phys.origin_configured = 0;
	updatephysorigins();
	assert(right->phys.origin_x_mm == 320);
	assert(right->phys.origin_y_mm == 360);

	place(left, 0, 0, 0, 0, 640, 360);

	/* Output scale and rotation change pixel dimensions, not millimetres. */
	wlr_output_state_init(&state);
	wlr_output_state_set_scale(&state, 1.25f);
	wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_90);
	assert(wlr_output_commit_state(right->wlr_output, &state));
	wlr_output_state_finish(&state);
	place(right, 1280, 0, 640, 100, 320, 180);
	warp(1279, 300);
	expect_image(right, -0.5 / right->phys.mm_per_px_x, 50 / right->phys.mm_per_px_y);
	motionnotify(6, NULL, 4, 0, 4, 0);
	assert(cursor_phys.mon == right);
	expect_image(right, 1.5 / right->phys.mm_per_px_x, 50 / right->phys.mm_per_px_y);

	/* Image changes must preserve the projected position, and hiding the
	 * pointer must not be undone by the next move. */
	warp(1279, 300);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "crosshair");
	expect_image(right, -0.5 / right->phys.mm_per_px_x, 50 / right->phys.mm_per_px_y);
	wlr_cursor_unset_image(cursor);
	warp(1278, 300);
	i = image(right);
	assert(!i->enabled);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	expect_image(right, -1 / right->phys.mm_per_px_x, 50 / right->phys.mm_per_px_y);
	assert(i->enabled);

	wlr_output_state_init(&state);
	wlr_output_state_set_scale(&state, 1);
	wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);
	assert(wlr_output_commit_state(right->wlr_output, &state));
	wlr_output_state_finish(&state);

	/* an explicit right screen anchors its automatic left neighbour. */
	place(left, 0, 0, 0, 0, 640, 360);
	place(right, 1280, 0, 500, 0, 320, 180);
	left->phys.origin_configured = 0;
	updatephysorigins();
	assert(left->phys.origin_x_mm == -140);
	warp(1100, 100);
	assert(image(left)->visible && !image(right)->visible);

	/* a 2x2 grid, with a physically larger top-left screen. */
	{
		Monitor *bottom_left = add_monitor("BOTTOM-LEFT");
		Monitor *bottom_right = add_monitor("BOTTOM-RIGHT");
		place(left, 0, 0, 0, 0, 640, 360);
		place(right, 1280, 0, 0, 0, 320, 180);
		place(bottom_left, 0, 720, 0, 0, 320, 180);
		place(bottom_right, 1280, 720, 0, 0, 320, 180);
		left->phys.origin_configured = right->phys.origin_configured = 0;
		bottom_left->phys.origin_configured = bottom_right->phys.origin_configured = 0;
		updatephysorigins();
		warp(800, 500);
		assert(image(left)->visible);
		assert(!image(right)->visible && !image(bottom_left)->visible && !image(bottom_right)->visible);
		wlr_output_destroy(bottom_right->wlr_output);
		wlr_output_destroy(bottom_left->wlr_output);
	}

	/* Removing a projected output must not leave stale monitor pointers. */
	wlr_output_destroy(right->wlr_output);
	warp(300, 300);
	expect_image(left, 300, 300);
	/* All screens disappear while the cursor is on the
	 * final screen. No global may retain that freed screen or its vout. */
	motionnotify(11, NULL, 1, 1, 1, 1);
	assert(selmon == left && hover.mon == left);
	assert(selvout == firstvout(left) && hover.vout == selvout);
	wlr_output_destroy(left->wlr_output);
	assert(!selmon && !selvout && !hover.mon && !hover.vout);
	assert(!cursor_phys.mon && !cursor_phys.last_mon);
	left = add_monitor("CURSOR-LEFT");
	warp(300, 300);
	motionnotify(11, NULL, 1, 1, 1, 1);
	assert(cursor_phys.mon == left && cursor_phys.last_mon == left);
	test_cleanup(runtime);
	puts("cursor: physical seams, gaps, scale, rotation and output removal passed");
	return 0;
}
