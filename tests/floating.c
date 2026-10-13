/* Exercise the actual compositor functions for floating sizes with a mapped Wayland window.
 * Include vwl.c to reach static functions without adding a test-only API. */
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
	int sockets[2], control[2], status, width, height, header;
	pid_t child;
	Client *c, *sibling;
	Monitor *m;
	VirtualOutput *vout;
	struct wlr_box full, resized;

	assert(mkdtemp(runtime));
	assert(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
	assert(setenv("WLR_BACKENDS", "headless", 1) == 0);
	assert(setenv("WLR_HEADLESS_OUTPUTS", "2", 1) == 0);
	assert(setenv("WLR_RENDERER", "pixman", 1) == 0);
	log_level = WLR_ERROR;
	setup();
	/* Use a private socket directory, never the running desktop's IPC socket. */
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
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	m = c->mon;
	vout = CLIENT_VOUT(c);
	full = vout->layout_geom;
	focusclient(c, 1);

	/* Mapping remembers the app's content size before tiling or fullscreen. */
	assert(c->float_width == 320 && c->float_height == 240 && !c->float_size_saved);
	client_command(control[0], 'u');
	setfloating(c, 1);
	expect_size(c, control[0], 320, 240);

	/* Preserve a user's size, including sizes larger than the configured factors. */
	resized = (struct wlr_box){full.x, full.y, full.width - 20, full.height - 20};
	resize(c, resized, 0);
	width = resized.width - 2 * (int)c->bw;
	height = resized.height - 2 * (int)c->bw;
	setfloating(c, 0);
	assert(c->float_size_saved && c->float_width == width && c->float_height == height);
	setfloating(c, 1);
	expect_size(c, control[0], width, height);
	setfloating(c, 0);
	setfloating(c, 1);
	expect_size(c, control[0], width, height);

	/* Fullscreen has its own saved rectangle and cannot replace the floating size. */
	resize(c, (struct wlr_box){full.x + 10, full.y + 10, 600, 400}, 0);
	resized = c->geom;
	client_command(control[0], 'f');
	assert(c->isfullscreen && wlr_box_equal(&c->prev, &resized));
	setfloating(c, 0);
	assert(c->isfloating && c->float_width == width && c->float_height == height);
	client_command(control[0], 'u');
	assert(wlr_box_equal(&c->geom, &resized));
	width = resized.width - 2 * (int)c->bw;
	height = resized.height - 2 * (int)c->bw;
	setfloating(c, 0);
	setfloating(c, 1);
	expect_size(c, control[0], width, height);

	/* A remap of the same window must not overwrite its remembered size. */
	setfloating(c, 0);
	client_command(control[0], 'c');
	client_command(control[0], 'h');
	client_command(control[0], 'm');
	assert(c->float_size_saved && c->float_width == width && c->float_height == height);
	setfloating(c, 1);
	expect_size(c, control[0], width, height);

	/* A smaller split output caps the restored size and keeps the window visible. */
	setfloating(c, 0);
	vout->rule_geom = (struct wlr_box){100, 100, 200, 180};
	arrangevout(m, &m->window_area);
	arrange(m);
	setfloating(c, 1);
	expect_size(c, control[0], 200 - 2 * (int)c->bw, 180 - 2 * (int)c->bw);
	assert(c->geom.x == vout->layout_geom.x && c->geom.y == vout->layout_geom.y);

	/* Tab headers also reduce the area available when restoring a saved size. */
	setfloating(c, 0);
	setlayout(&(Arg){.v = &layouts[1]});
	header = tabhdr_header_height();
	assert(header > 0 && header < 180);
	setfloating(c, 1);
	expect_size(c, control[0], 200 - 2 * (int)c->bw, 180 - header - 2 * (int)c->bw);
	assert(c->geom.y == vout->layout_geom.y + (tabhdr_position_value() == TABHDR_TOP ? header : 0));

	/* First-time floating obeys configured factors on each axis. */
	setlayout(&(Arg){.v = &layouts[0]});
	focusclient(c, 1);
	client_command(control[0], 'n');
	sibling = wl_container_of(clients.prev, sibling, link);
	assert(sibling != c && !sibling->float_size_saved);
	setfloating(sibling, 1);
	width = floating_window_width_factor > 0 && floating_window_width_factor <= 1
				? (int)roundf(200 * floating_window_width_factor)
				: 200;
	height = floating_window_height_factor > 0 && floating_window_height_factor <= 1
				 ? (int)roundf(180 * floating_window_height_factor)
				 : 180;
	expect_size(sibling, control[0], MAX(1, width - 2 * (int)sibling->bw), MAX(1, height - 2 * (int)sibling->bw));
	client_command(control[0], 'd');

	/* Windows which open floating save their latest size when tiled too. */
	vout->rule_geom = (struct wlr_box){0};
	arrangevout(m, &m->window_area);
	arrange(m);
	client_command(control[0], 's');
	sibling = wl_container_of(clients.prev, sibling, link);
	assert(sibling != c && sibling->isfloating);
	assert(sibling->float_width == 320 && sibling->float_height == 240);
	resize(sibling, (struct wlr_box){full.x, full.y, 420, 310}, 0);
	width = 420 - 2 * (int)sibling->bw;
	height = 310 - 2 * (int)sibling->bw;
	setfloating(sibling, 0);
	setfloating(sibling, 1);
	expect_size(sibling, control[0], width, height);
	client_command(control[0], 'd');

	close(control[0]);
	while (!wl_list_empty(&clients)) pump();
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	cleanup();
	assert(rmdir(runtime) == 0);
	puts("floating: initial size, saved size, borders, fullscreen and output bounds passed");
	return 0;
}
