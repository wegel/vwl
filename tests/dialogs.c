/* Exercise dialog placement with actual mapped Wayland windows.
 * Include vwl.c to reach static functions without adding a test-only API. */
#include "compositor.h"

static Client *
dialog_for(Client *parent)
{
	Client *c;
	wl_list_for_each(c, &clients, link) {
		if (client_get_parent(c) == parent)
			return c;
	}
	assert(0 && "missing dialog");
	return NULL;
}

static void
expect_dialog(Client *c, Client *parent, int x, int y, int width, int height)
{
	assert(c->isfloating && c->ws == parent->ws && c->mon == parent->mon);
	assert(c->geom.x == x && c->geom.y == y);
	assert(c->geom.width == width + 2 * (int)c->bw);
	assert(c->geom.height == height + 2 * (int)c->bw);
	assert(c->scene->node.x == x && c->scene->node.y == y);
}

/* X11 coordinates are screen positions, including positions on either axis. */
static void
check_x11_policy(void)
{
#ifdef XWAYLAND
	Monitor mon = {.window_area = {100, -100, 800, 600}};
	VirtualOutput vout = {.mon = &mon, .layout_geom = {500, -100, 400, 600}};
	Workspace ws = {.vout = &vout};
	Client parent = {.mon = &mon, .geom = {600, 0, 200, 200}};
	Client child = {.type = X11, .isfloating = 1, .geom = {0, 0, 100, 100}};
	struct wlr_box positions[] = {{720, 50, 100, 100}, {0, 50, 100, 100}, {720, 0, 100, 100}};
	size_t i;

	placechild(&child, &parent, &ws);
	assert(child.geom.x == 650 && child.geom.y == 50);
	for (i = 0; i < LENGTH(positions); i++) {
		child.geom = positions[i];
		placechild(&child, &parent, &ws);
		assert(wlr_box_equal(&child.geom, &positions[i]));
	}
	child.geom = (struct wlr_box){0, 0, 100, 100};
	child.isfloating = 0;
	placechild(&child, &parent, &ws);
	assert(child.geom.x == 0 && child.geom.y == 0);
#endif
}

int
main(void)
{
	char runtime[] = "/tmp/vwl-dialogs-XXXXXX";
	int bw;
	struct TestClient app;
	Client *parent, *dialog;
	Monitor *m;
	VirtualOutput *vout;
	struct wlr_box area, moved;

	check_x11_policy();
	test_setup(runtime, "2", NULL);
	app = test_client_start(fullscreen_test_client, 20);
	client_command(app.control, 'r');
	parent = wl_container_of(clients.next, parent, link);
	client_command(app.control, 'u');
	setfloating(parent, 1);
	m = parent->mon;
	vout = CLIENT_VOUT(parent);
	area = vout->layout_geom;
	resize(parent, (struct wlr_box){area.x + 100, area.y + 100, 600, 400}, 0);
	client_command(app.control, 'p');
	dialog = dialog_for(parent);
	bw = (int)dialog->bw;
	expect_dialog(dialog, parent, area.x + 240 - bw, area.y + 180 - bw, 320, 240);
	client_command(app.control, 'd');

	/* A shadow changes surface-local x/y, not the desired screen position. */
	client_command(app.control, 's');
	dialog = dialog_for(parent);
	assert(dialog->surface.xdg->geometry.x == 12 && dialog->surface.xdg->geometry.y == 12);
	expect_dialog(dialog, parent, area.x + 240 - bw, area.y + 180 - bw, 320, 240);
	moved = (struct wlr_box){area.x + 25, area.y + 35, dialog->geom.width, dialog->geom.height};
	resize(dialog, moved, 0);
	client_command(app.control, 'c');
	assert(wlr_box_equal(&dialog->geom, &moved));
	client_command(app.control, 'd');

	/* Use a different monitor with a negative origin and a split virtual screen. */
	m = dirtomonfrom(m, WLR_DIRECTION_RIGHT);
	assert(m && m != parent->mon);
	wlr_output_layout_add(output_layout, m->wlr_output, -1600, -100);
	updatemons(NULL, NULL);
	vout = firstvout(m);
	vout->rule_geom.x = m->window_area.width / 2;
	vout->rule_geom.width = m->window_area.width / 2;
	arrangevout(m, &m->window_area);
	assert(ipc_move_workspace_to_vout(parent->ws, vout) == 0);
	area = vout->layout_geom;
	assert(area.x < 0 && area.y < 0);

	/* A small parent at either edge must not push the dialog off its screen. */
	resize(parent, (struct wlr_box){area.x, area.y, 100, 100}, 0);
	client_command(app.control, 's');
	dialog = dialog_for(parent);
	expect_dialog(dialog, parent, area.x, area.y, 320, 240);
	client_command(app.control, 'd');
	resize(parent, (struct wlr_box){area.x + area.width - 100, area.y + area.height - 100, 100, 100}, 0);
	client_command(app.control, 'p');
	dialog = dialog_for(parent);
	expect_dialog(dialog, parent, area.x + area.width - 320 - 2 * bw, area.y + area.height - 240 - 2 * bw, 320,
			240);
	client_command(app.control, 'd');

	/* Keep an oversized dialog's requested size and expose its top-left corner. */
	client_command(app.control, 'o');
	dialog = dialog_for(parent);
	expect_dialog(dialog, parent, area.x, area.y, 2000, 1500);
	client_command(app.control, 'd');

	test_client_finish(app);
	test_cleanup(runtime);
	puts("dialogs: all compositor checks passed");
	return 0;
}
