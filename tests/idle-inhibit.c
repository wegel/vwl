/* Keep an idle inhibitor alive after its window disappears, as browsers do. */
#include "compositor.h"

int
main(void)
{
	char runtime[] = "/tmp/vwl-idle-inhibit-XXXXXX";

	struct TestClient app;
	Client *c;
	Monitor *m;
	struct wlr_surface *surface;

	assert(!bypass_surface_visibility);
	test_setup(runtime, "1", NULL);
	app = test_client_start(fullscreen_test_client, 20);
	client_command(app.control, 'r');
	client_command(app.control, 'u');
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	m = c->mon;
	surface = client_surface(c);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	client_command(app.control, 'i');
	assert(idle_notifier->WLR_PRIVATE.inhibited);

	/* Hidden workspaces must stop inhibiting idle, then resume when visible. */
	wlr_scene_node_set_enabled(&c->scene->node, false);
	checkidleinhibitor(NULL);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	wlr_scene_node_set_enabled(&c->scene->node, true);
	checkidleinhibitor(NULL);
	assert(idle_notifier->WLR_PRIVATE.inhibited);

	/* The app retains its surface and inhibitor while unmapping/remapping. */
	client_command(app.control, 'h');
	assert(!surface->mapped);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	assert(!surface->data && !c->scene && !c->scene_surface);
	assert(wl_list_length(&idle_inhibit_mgr->inhibitors) == 1);
	arrange(m);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	client_command(app.control, 'm');
	assert(surface->mapped && surface->data == c->scene);
	assert(idle_notifier->WLR_PRIVATE.inhibited);

	/* Match the crash: destroy one window's role, then remove another window
	 * while the first surface's keep-awake request is still alive. */
	client_command(app.control, 'n');
	client_command(app.control, 't');
	assert(!surface->mapped && !surface->role_resource && !surface->data);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	assert(wl_list_length(&idle_inhibit_mgr->inhibitors) == 1);
	client_command(app.control, 'd');
	assert(wl_list_empty(&clients));
	arrange(m);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	client_command(app.control, 'x');
	assert(wl_list_empty(&idle_inhibit_mgr->inhibitors));
	assert(!idle_notifier->WLR_PRIVATE.inhibited);

	test_client_finish(app);
	test_cleanup(runtime);
	puts("idle-inhibit: all compositor checks passed");
	return 0;
}
