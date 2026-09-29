/* Exercise workspace moves and display reconnects with a mapped Wayland app. */
#undef NDEBUG
#define main vwl_main
#include "../vwl.c"
#undef main

#include <assert.h>
#include <sys/socket.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>

int fullscreen_test_client(int socket_fd, int control_fd);

static void
pump(void)
{
	wl_display_flush_clients(dpy);
	assert(wl_event_loop_dispatch(event_loop, 5) >= 0);
}

static void
client_command(int fd, char command)
{
	char reply;
	ssize_t count;

	if (command != 'r')
		assert(write(fd, &command, 1) == 1);
	while ((count = recv(fd, &reply, 1, MSG_DONTWAIT)) < 0) {
		assert(errno == EAGAIN || errno == EWOULDBLOCK);
		pump();
	}
	assert(count == 1 && reply == command);
}

static Monitor *
add_monitor(const char *name)
{
	struct wlr_backend *headless = wlr_headless_backend_create(event_loop);
	struct wlr_output *output;

	assert(headless);
	output = wlr_headless_add_output(headless, 1280, 720);
	assert(output);
	wlr_output_set_name(output, name);
	assert(wlr_multi_backend_add(backend, headless));
	assert(wlr_backend_start(headless));
	assert(monitorbyname(name));
	return monitorbyname(name);
}

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
	int sockets[2], control[2], status;
	pid_t child;
	Client *c, *sibling;
	Monitor *main_monitor, *left_monitor, *other_monitor;
	VirtualOutput *main_vout, *left_vout;
	Workspace *ws, *hidden;
	const unsigned int left_defaults[] = {1};
	float saved_mfact;

	assert(mkdtemp(runtime));
	assert(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
	assert(setenv("WLR_BACKENDS", "headless", 1) == 0);
	assert(setenv("WLR_HEADLESS_OUTPUTS", "0", 1) == 0);
	assert(setenv("WLR_RENDERER", "pixman", 1) == 0);
	log_level = WLR_ERROR;
	setup();
	ipc_init();
	assert(wlr_backend_start(backend));
	main_monitor = add_monitor("TEST-MAIN");
	left_monitor = add_monitor("TEST-LEFT");
	main_vout = firstvout(main_monitor);
	left_vout = firstvout(left_monitor);
	ws = wsbyid(1);
	hidden = wsbyid(3);
	wsattach(main_vout, hidden);
	wsattach(left_vout, ws);
	assert(ipc_set_workspace_by_id(ws->id) == 0);

	signal(SIGCHLD, SIG_DFL);
	alarm(20);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, control) == 0);
	child = fork();
	assert(child >= 0);
	if (child == 0) {
		close(sockets[0]);
		close(control[0]);
		_exit(fullscreen_test_client(sockets[1], control[1]));
	}
	close(sockets[1]);
	close(control[1]);
	assert(wl_client_create(dpy, sockets[0]));
	client_command(control[0], 'r');
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	client_command(control[0], 'u');
	expect_window(c, ws, left_monitor);
	assert(ipc_move_workspace_to_vout(ws, main_vout) == 0);
	expect_window(c, ws, main_monitor);

	/* Reapplying the left display's defaults must keep the user's move. */
	attachvoutworkspaces(left_vout, left_defaults, LENGTH(left_defaults));
	assert(ipc_set_workspace_by_id(ws->id) == 0);
	expect_window(c, ws, main_monitor);
	client_command(control[0], 'n');
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

	close(control[0]);
	while (!wl_list_empty(&clients)) pump();
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	cleanup();
	assert(rmdir(runtime) == 0);
	puts("workspaces: all compositor checks passed");
	return 0;
}
