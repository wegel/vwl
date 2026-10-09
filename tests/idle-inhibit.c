/* Keep an idle inhibitor alive after its window disappears, as browsers do. */
#undef NDEBUG
#define main vwl_main
#include "../vwl.c"
#undef main

#include <assert.h>
#include <sys/socket.h>

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

int
main(void)
{
	char runtime[] = "/tmp/vwl-idle-inhibit-XXXXXX";
	int sockets[2], control[2], status;
	pid_t child;
	Client *c;
	Monitor *m;
	struct wlr_surface *surface;

	assert(!bypass_surface_visibility);
	assert(mkdtemp(runtime));
	assert(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
	assert(setenv("WLR_BACKENDS", "headless", 1) == 0);
	assert(setenv("WLR_HEADLESS_OUTPUTS", "1", 1) == 0);
	assert(setenv("WLR_RENDERER", "pixman", 1) == 0);
	log_level = WLR_ERROR;
	setup();
	ipc_init();
	assert(wlr_backend_start(backend));
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
	client_command(control[0], 'u');
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	m = c->mon;
	surface = client_surface(c);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	client_command(control[0], 'i');
	assert(idle_notifier->WLR_PRIVATE.inhibited);

	/* Hidden workspaces must stop inhibiting idle, then resume when visible. */
	wlr_scene_node_set_enabled(&c->scene->node, false);
	checkidleinhibitor(NULL);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	wlr_scene_node_set_enabled(&c->scene->node, true);
	checkidleinhibitor(NULL);
	assert(idle_notifier->WLR_PRIVATE.inhibited);

	/* The app retains its surface and inhibitor while unmapping/remapping. */
	client_command(control[0], 'h');
	assert(!surface->mapped);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	assert(!surface->data && !c->scene && !c->scene_surface);
	assert(wl_list_length(&idle_inhibit_mgr->inhibitors) == 1);
	arrange(m);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	client_command(control[0], 'm');
	assert(surface->mapped && surface->data == c->scene);
	assert(idle_notifier->WLR_PRIVATE.inhibited);

	/* Match the crash: destroy one window's role, then remove another window
	 * while the first surface's keep-awake request is still alive. */
	client_command(control[0], 'n');
	client_command(control[0], 't');
	assert(!surface->mapped && !surface->role_resource && !surface->data);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	assert(wl_list_length(&idle_inhibit_mgr->inhibitors) == 1);
	client_command(control[0], 'd');
	assert(wl_list_empty(&clients));
	arrange(m);
	assert(!idle_notifier->WLR_PRIVATE.inhibited);
	client_command(control[0], 'x');
	assert(wl_list_empty(&idle_inhibit_mgr->inhibitors));
	assert(!idle_notifier->WLR_PRIVATE.inhibited);

	close(control[0]);
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	cleanup();
	assert(rmdir(runtime) == 0);
	puts("idle-inhibit: all compositor checks passed");
	return 0;
}
