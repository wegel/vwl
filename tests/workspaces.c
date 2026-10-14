/* Exercise workspace moves and display reconnects with a mapped Wayland app. */
#include "compositor.h"

static void
expect_window(Client *c, Workspace *ws, Monitor *m)
{
	assert(c->ws == ws);
	assert(ws->vout && ws->vout->mon == m);
	assert(c->mon == m);
	assert(ws->vout->ws == ws);
	assert(c->scene->node.enabled);
	assert(VISIBLEON(c, m));
	assert(c->geom.x >= ws->vout->layout_geom.x);
	assert(c->geom.y >= ws->vout->layout_geom.y);
	assert(c->geom.x + c->geom.width <= ws->vout->layout_geom.x + ws->vout->layout_geom.width);
	assert(c->geom.y + c->geom.height <= ws->vout->layout_geom.y + ws->vout->layout_geom.height);
}

static void
expect_hidden(Client *c, Workspace *ws, Monitor *m)
{
	assert(c->ws == ws && ws->vout && ws->vout->mon == m);
	assert(c->mon == m);
	assert(ws->vout->ws != ws);
	assert(!c->scene->node.enabled);
}

int
main(void)
{
	char runtime[] = "/tmp/vwl-workspaces-XXXXXX";

	struct TestClient app;
	Client *c, *sibling;
	Monitor *main_monitor, *left_monitor, *other_monitor;
	VirtualOutput *main_vout, *left_vout;
	Workspace *ws, *hidden;
	const unsigned int left_defaults[] = {1};
	float saved_mfact;

	test_setup(runtime, "0", NULL);
	main_monitor = add_monitor("TEST-MAIN");
	left_monitor = add_monitor("TEST-LEFT");
	main_vout = firstvout(main_monitor);
	left_vout = firstvout(left_monitor);
	ws = wsbyid(1);
	hidden = wsbyid(3);
	wsattach(main_vout, hidden);
	wsattach(left_vout, ws);
	assert(ipc_set_workspace_by_id(ws->id) == 0);

	app = test_client_start(fullscreen_test_client, 20);
	client_command(app.control, 'r');
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	client_command(app.control, 'u');
	expect_window(c, ws, left_monitor);
	assert(ipc_move_workspace_to_vout(ws, main_vout) == 0);
	expect_window(c, ws, main_monitor);

	/* Reapplying the left display's defaults must keep the user's move. */
	attachvoutworkspaces(left_vout, left_defaults, LENGTH(left_defaults));
	assert(ipc_set_workspace_by_id(ws->id) == 0);
	expect_window(c, ws, main_monitor);
	client_command(app.control, 'n');
	assert(wl_list_length(&clients) == 2);
	sibling = wl_container_of(clients.prev, sibling, link);
	assert(sibling != c);
	setworkspace(sibling, hidden);
	expect_hidden(sibling, hidden, main_monitor);

	setmfact(&(Arg){.f = 1.63f});
	saved_mfact = main_vout->mfact;
	/* A reconnect must restore the active workspace, not the highest ID. */
	wlr_output_destroy(main_monitor->wlr_output);
	assert(ws->was_orphaned && hidden->was_orphaned);
	assert(ws->vout->mon == left_monitor && c->mon == left_monitor);
	main_monitor = add_monitor("TEST-MAIN");
	main_vout = firstvout(main_monitor);
	expect_window(c, ws, main_monitor);
	expect_hidden(sibling, hidden, main_monitor);
	assert(hidden->vout == main_vout && !hidden->was_orphaned);
	assert(main_vout->mfact == saved_mfact);
	assert(!ws->was_orphaned);

	/* Both displays disappear; the left one returns before the main one. */
	wlr_output_destroy(main_monitor->wlr_output);
	wlr_output_destroy(left_monitor->wlr_output);
	assert(!ws->vout && !c->mon && ws->was_orphaned);
	assert(wsfindfree() != ws && wsfindfree() != hidden);
	left_monitor = add_monitor("TEST-LEFT");
	left_vout = firstvout(left_monitor);
	attachvoutworkspaces(left_vout, left_defaults, LENGTH(left_defaults));
	assert(!ws->vout && ws->was_orphaned);
	main_monitor = add_monitor("TEST-MAIN");
	main_vout = firstvout(main_monitor);
	expect_window(c, ws, main_monitor);
	expect_hidden(sibling, hidden, main_monitor);
	assert(main_vout->mfact == saved_mfact);

	/* Reverse the disconnect order, then bring the main display back first. */
	wlr_output_destroy(left_monitor->wlr_output);
	wlr_output_destroy(main_monitor->wlr_output);
	main_monitor = add_monitor("TEST-MAIN");
	main_vout = firstvout(main_monitor);
	expect_window(c, ws, main_monitor);
	expect_hidden(sibling, hidden, main_monitor);
	left_monitor = add_monitor("TEST-LEFT");
	attachvoutworkspaces(firstvout(left_monitor), left_defaults, LENGTH(left_defaults));
	expect_window(c, ws, main_monitor);

	/* A deliberate move while a display is absent replaces its old home. */
	wlr_output_destroy(main_monitor->wlr_output);
	other_monitor = add_monitor("TEST-OTHER");
	assert(ipc_move_workspace_to_vout(ws, firstvout(other_monitor)) == 0);
	main_monitor = add_monitor("TEST-MAIN");
	expect_window(c, ws, other_monitor);
	assert(!ws->was_orphaned);

	test_client_finish(app);
	test_cleanup(runtime);
	puts("workspaces: all compositor checks passed");
	return 0;
}
