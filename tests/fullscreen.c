/* Exercise the actual compositor functions with a mapped Wayland window.
 * Include vwl.c to reach static functions without adding a test-only API. */
#include "compositor.h"

static void
expect_mode(Client *c, int mode, struct wlr_box box)
{
	assert(c->isfullscreen == (mode != FS_NONE));
	assert(c->fullscreen_mode == mode);
	assert(wlr_box_equal(&c->geom, &box));
	if (mode != FS_NONE) {
		assert(c->bw == 0);
		assert(c->scene->node.parent == layers[mode == FS_MONITOR ? LyrFS : LyrTop]);
	}
}

int
main(void)
{
	char runtime[] = "/tmp/vwl-fullscreen-XXXXXX";

	struct TestClient app;
	Client *c;
	Monitor *m, *other_monitor;
	VirtualOutput *vout, *half;
	Workspace *ws;
	struct wlr_box original, full, region, tab_area;
	struct wlr_output_state output_state;
	int header;

	test_setup(runtime, "2", NULL);
	app = test_client_start(fullscreen_test_client, 20);
	client_command(app.control, 'r');
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	m = c->mon;
	vout = CLIENT_VOUT(c);
	ws = c->ws;
	full = m->monitor_area;
	focusclient(c, 1);

	/* The app requested fullscreen before mapping; scope waits for its output. */
	expect_mode(c, FS_MONITOR, full);
	assert(c->prev.width > 0 && c->prev.height > 0);
	client_command(app.control, 'u');
	assert(!c->isfullscreen && c->bw == borderpx);
	/* A single tiled window still enters fullscreen even though it fills the output. */
	togglefullscreen(NULL);
	expect_mode(c, FS_MONITOR, full);
	togglefullscreen(NULL);
	assert(!c->isfullscreen);

	setfloating(c, 1);
	original = (struct wlr_box){full.x + 30, full.y + 40, 400, 300};
	resize(c, original, 0);
	togglefullscreen(NULL);
	expect_mode(c, FS_MONITOR, full);
	client_command(app.control, 'f');
	client_command(app.control, 'f');
	expect_mode(c, FS_MONITOR, full);
	assert(wlr_box_equal(&c->prev, &original));
	togglefullscreen(NULL);
	expect_mode(c, FS_NONE, original);
	assert(c->bw == floatborderpx);
	client_command(app.control, 'u');
	expect_mode(c, FS_NONE, original);

	/* Split output: preserve the original floating rectangle across both steps. */
	vout->rule_geom.width = full.width / 2;
	arrangevout(m, &full);
	region = vout->layout_geom;
	togglefullscreen(NULL);
	expect_mode(c, FS_VIRTUAL, region);
	togglefullscreen(NULL);
	expect_mode(c, FS_MONITOR, full);
	client_command(app.control, 'f');
	expect_mode(c, FS_MONITOR, full);
	togglefullscreen(NULL);
	expect_mode(c, FS_NONE, original);

	/* Removing a split resizes virtual fullscreen, then the next press exits. */
	togglefullscreen(NULL);
	vout->rule_geom = (struct wlr_box){0};
	arrangevout(m, &full);
	arrange(m);
	expect_mode(c, FS_VIRTUAL, full);
	togglefullscreen(NULL);
	expect_mode(c, FS_NONE, original);

	/* Reserved panel space remains a useful fullscreen step. */
	m->window_area = full;
	m->window_area.y += 43;
	m->window_area.height -= 43;
	arrangevout(m, &m->window_area);
	region = vout->layout_geom;
	togglefullscreen(NULL);
	expect_mode(c, FS_VIRTUAL, region);
	togglefullscreen(NULL);
	expect_mode(c, FS_MONITOR, full);
	togglefullscreen(NULL);
	m->window_area = full;
	arrangevout(m, &full);

	/* Adding/removing panel space while fullscreen refreshes its rectangle. */
	vout->rule_geom.width = full.width / 2;
	arrangevout(m, &full);
	togglefullscreen(NULL);
	m->window_area.height -= 43;
	arrangevout(m, &m->window_area);
	arrange(m);
	expect_mode(c, FS_VIRTUAL, vout->layout_geom);
	m->window_area = full;
	vout->rule_geom = (struct wlr_box){0};
	arrangevout(m, &full);
	arrange(m);
	togglefullscreen(NULL);
	expect_mode(c, FS_NONE, original);

	/* Layout changes resize fullscreen without changing its selected scope. */
	setfloating(c, 0);
	setlayout(&(Arg){.v = &layouts[1]});
	tab_area = full;
	header = tabhdr_header_height();
	assert(header > 0 && header < full.height);
	tab_area.height -= header;
	if (tabhdr_position_value() == TABHDR_TOP)
		tab_area.y += header;
	togglefullscreen(NULL);
	expect_mode(c, FS_VIRTUAL, tab_area);
	assert(!tiling_locked_by_fullscreen(c));
	setlayout(&(Arg){.v = &layouts[0]});
	expect_mode(c, FS_VIRTUAL, full);
	setlayout(&(Arg){.v = &layouts[1]});
	expect_mode(c, FS_VIRTUAL, tab_area);
	togglefullscreen(NULL);
	expect_mode(c, FS_MONITOR, full);
	assert(tiling_locked_by_fullscreen(c));
	togglefullscreen(NULL);
	/* A display too short to show the header still needs tab switching. */
	m->monitor_area.height = header;
	vout->layout_geom = m->monitor_area;
	togglefullscreen(NULL);
	expect_mode(c, FS_VIRTUAL, m->monitor_area);
	assert(!tiling_locked_by_fullscreen(c));
	togglefullscreen(NULL);
	expect_mode(c, FS_MONITOR, m->monitor_area);
	togglefullscreen(NULL);
	m->monitor_area = full;
	arrangevout(m, &full);
	setlayout(&(Arg){.v = &layouts[0]});

	/* Move the fullscreen workspace to a half-output on the same monitor. */
	setfloating(c, 1);
	resize(c, original, 0);
	half = createvout(m, "test-half");
	half->rule_geom.width = full.width / 2;
	arrangevout(m, &full);
	togglefullscreen(NULL);
	assert(ipc_move_workspace_to_vout(ws, half) == 0);
	focusclient(c, 1);
	expect_mode(c, FS_MONITOR, full);
	togglefullscreen(NULL);
	expect_mode(c, FS_NONE, original);
	togglefullscreen(NULL);
	expect_mode(c, FS_VIRTUAL, half->layout_geom);
	assert(ipc_move_workspace_to_vout(ws, vout) == 0);
	focusclient(c, 1);
	expect_mode(c, FS_VIRTUAL, full);
	togglefullscreen(NULL);
	expect_mode(c, FS_NONE, original);

	/* Moving between physical monitors keeps the saved size and chosen scope. */
	other_monitor = dirtomonfrom(m, WLR_DIRECTION_RIGHT);
	assert(other_monitor && other_monitor != m);
	togglefullscreen(NULL);
	assert(ipc_move_workspace_to_vout(ws, firstvout(other_monitor)) == 0);
	focusclient(c, 1);
	expect_mode(c, FS_MONITOR, other_monitor->monitor_area);
	assert(wlr_box_equal(&c->prev, &original));
	/* Reconfigure a fullscreen monitor using scale, rotation, and negative coordinates. */
	wlr_output_state_init(&output_state);
	wlr_output_state_set_scale(&output_state, 1.25f);
	wlr_output_state_set_transform(&output_state, WL_OUTPUT_TRANSFORM_90);
	assert(wlr_output_commit_state(other_monitor->wlr_output, &output_state));
	wlr_output_state_finish(&output_state);
	wlr_output_layout_add(output_layout, other_monitor->wlr_output, -1600, -100);
	updatemons(NULL, NULL);
	expect_mode(c, FS_MONITOR, other_monitor->monitor_area);
	assert(wlr_box_equal(&c->prev, &original));
	togglefullscreen(NULL);
	assert(c->geom.width == original.width && c->geom.height == original.height);
	assert(ipc_move_workspace_to_vout(ws, vout) == 0);
	focusclient(c, 1);
	resize(c, original, 0);

	/* New ordinary windows exit physical fullscreen but preserve virtual fullscreen. */
	togglefullscreen(NULL);
	client_command(app.control, 'n');
	expect_mode(c, FS_NONE, original);
	client_command(app.control, 'd');
	focusclient(c, 1);
	vout->rule_geom.width = full.width / 2;
	arrangevout(m, &full);
	togglefullscreen(NULL);
	client_command(app.control, 'n');
	expect_mode(c, FS_VIRTUAL, vout->layout_geom);
	client_command(app.control, 'd');
	focusclient(c, 1);
	client_command(app.control, 'u');
	expect_mode(c, FS_NONE, original);

	/* An app can leave fullscreen while its monitor/workspace is unavailable. */
	togglefullscreen(NULL);
	setworkspace(c, NULL);
	client_command(app.control, 'u');
	assert(wlr_box_equal(&c->geom, &original));
	setworkspace(c, ws);
	expect_mode(c, FS_NONE, original);
	assert(c->bw == floatborderpx);

	test_client_finish(app);
	test_cleanup(runtime);
	puts("fullscreen: all compositor checks passed");
	return 0;
}
