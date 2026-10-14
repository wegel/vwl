/* Exercise the actual compositor functions for floating sizes with a mapped Wayland window.
 * Include vwl.c to reach static functions without adding a test-only API. */
#include "compositor.h"

static void
expect_size(Client *c, int fd, int width, int height)
{
	client_command(fd, 'c');
	assert(c->isfloating && !c->isfullscreen);
	assert(c->geom.width == width + 2 * (int)c->bw);
	assert(c->geom.height == height + 2 * (int)c->bw);
	assert(c->surface.xdg->geometry.width == width);
	assert(c->surface.xdg->geometry.height == height);
}

int
main(void)
{
	char runtime[] = "/tmp/vwl-floating-XXXXXX";
	int width, height, header;
	struct TestClient app;
	Client *c, *sibling;
	Monitor *m;
	VirtualOutput *vout;
	struct wlr_box full, resized;

	test_setup(runtime, "2", NULL);
	app = test_client_start(fullscreen_test_client, 20);
	client_command(app.control, 'r');
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	m = c->mon;
	vout = CLIENT_VOUT(c);
	full = vout->layout_geom;
	focusclient(c, 1);

	/* Mapping remembers the app's content size before tiling or fullscreen. */
	assert(c->float_width == 320 && c->float_height == 240 && !c->float_size_saved);
	client_command(app.control, 'u');
	setfloating(c, 1);
	expect_size(c, app.control, 320, 240);

	/* Preserve a user's size, including sizes larger than the configured factors. */
	resized = (struct wlr_box){full.x, full.y, full.width - 20, full.height - 20};
	resize(c, resized, 0);
	width = resized.width - 2 * (int)c->bw;
	height = resized.height - 2 * (int)c->bw;
	setfloating(c, 0);
	assert(c->float_size_saved && c->float_width == width && c->float_height == height);
	setfloating(c, 1);
	expect_size(c, app.control, width, height);
	setfloating(c, 0);
	setfloating(c, 1);
	expect_size(c, app.control, width, height);

	/* Fullscreen has its own saved rectangle and cannot replace the floating size. */
	resize(c, (struct wlr_box){full.x + 10, full.y + 10, 600, 400}, 0);
	resized = c->geom;
	client_command(app.control, 'f');
	assert(c->isfullscreen && wlr_box_equal(&c->prev, &resized));
	setfloating(c, 0);
	assert(c->isfloating && c->float_width == width && c->float_height == height);
	client_command(app.control, 'u');
	assert(wlr_box_equal(&c->geom, &resized));
	width = resized.width - 2 * (int)c->bw;
	height = resized.height - 2 * (int)c->bw;
	setfloating(c, 0);
	setfloating(c, 1);
	expect_size(c, app.control, width, height);

	/* A remap of the same window must not overwrite its remembered size. */
	setfloating(c, 0);
	client_command(app.control, 'c');
	client_command(app.control, 'h');
	client_command(app.control, 'm');
	assert(c->float_size_saved && c->float_width == width && c->float_height == height);
	setfloating(c, 1);
	expect_size(c, app.control, width, height);

	/* A smaller split output caps the restored size and keeps the window visible. */
	setfloating(c, 0);
	vout->rule_geom = (struct wlr_box){100, 100, 200, 180};
	arrangevout(m, &m->window_area);
	arrange(m);
	setfloating(c, 1);
	expect_size(c, app.control, 200 - 2 * (int)c->bw, 180 - 2 * (int)c->bw);
	assert(c->geom.x == vout->layout_geom.x && c->geom.y == vout->layout_geom.y);

	/* Tab headers also reduce the area available when restoring a saved size. */
	setfloating(c, 0);
	setlayout(&(Arg){.v = &layouts[1]});
	header = tabhdr_header_height();
	assert(header > 0 && header < 180);
	setfloating(c, 1);
	expect_size(c, app.control, 200 - 2 * (int)c->bw, 180 - header - 2 * (int)c->bw);
	assert(c->geom.y == vout->layout_geom.y + (tabhdr_position_value() == TABHDR_TOP ? header : 0));

	/* First-time floating obeys configured factors on each axis. */
	setlayout(&(Arg){.v = &layouts[0]});
	focusclient(c, 1);
	client_command(app.control, 'n');
	sibling = wl_container_of(clients.prev, sibling, link);
	assert(sibling != c && !sibling->float_size_saved);
	setfloating(sibling, 1);
	width = floating_window_width_factor > 0 && floating_window_width_factor <= 1
				? (int)roundf(200 * floating_window_width_factor)
				: 200;
	height = floating_window_height_factor > 0 && floating_window_height_factor <= 1
				 ? (int)roundf(180 * floating_window_height_factor)
				 : 180;
	expect_size(sibling, app.control, MAX(1, width - 2 * (int)sibling->bw), MAX(1, height - 2 * (int)sibling->bw));
	client_command(app.control, 'd');

	/* Windows which open floating save their latest size when tiled too. */
	vout->rule_geom = (struct wlr_box){0};
	arrangevout(m, &m->window_area);
	arrange(m);
	client_command(app.control, 's');
	sibling = wl_container_of(clients.prev, sibling, link);
	assert(sibling != c && sibling->isfloating);
	assert(sibling->float_width == 320 && sibling->float_height == 240);
	resize(sibling, (struct wlr_box){full.x, full.y, 420, 310}, 0);
	width = 420 - 2 * (int)sibling->bw;
	height = 310 - 2 * (int)sibling->bw;
	setfloating(sibling, 0);
	setfloating(sibling, 1);
	expect_size(sibling, app.control, width, height);
	client_command(app.control, 'd');

	test_client_finish(app);
	test_cleanup(runtime);
	puts("floating: initial size, saved size, borders, fullscreen and output bounds passed");
	return 0;
}
