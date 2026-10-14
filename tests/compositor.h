/* Shared private compositor and client process helpers for integration tests. */
#ifndef TEST_COMPOSITOR_H
#define TEST_COMPOSITOR_H
#undef NDEBUG
#define main vwl_main
#include "../vwl.c"
#undef main
#include <assert.h>
#include <sys/socket.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>

int fullscreen_test_client(int socket_fd, int control_fd);

struct TestClient {
	pid_t pid;
	int control;
};

static inline void
pump(void)
{
	wl_display_flush_clients(dpy);
	assert(wl_event_loop_dispatch(event_loop, 5) >= 0);
}

static inline void
receive(int fd, void *data, size_t size)
{
	ssize_t count;
	size_t used = 0;
	while (used < size) {
		count = recv(fd, (char *)data + used, size - used, MSG_DONTWAIT);
		if (count < 0) {
			assert(errno == EAGAIN || errno == EWOULDBLOCK);
			pump();
		} else {
			assert(count > 0);
			used += count;
		}
	}
}

static inline void
client_command(int fd, char command)
{
	char reply;
	if (command != 'r')
		assert(write(fd, &command, 1) == 1);
	receive(fd, &reply, 1);
	assert(reply == command);
}

static inline void
test_setup(char *runtime, const char *outputs, const char *renderer)
{
	assert(mkdtemp(runtime));
	assert(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
	assert(setenv("WLR_BACKENDS", "headless", 1) == 0);
	assert(setenv("WLR_HEADLESS_OUTPUTS", outputs, 1) == 0);
	assert(setenv("WLR_RENDERER", renderer ? renderer : "pixman", 1) == 0);
	log_level = WLR_ERROR;
	setup();
	/* Never connect to the running desktop's IPC socket. */
	ipc_init();
	assert(wlr_backend_start(backend));
}

static inline struct TestClient
test_client_start(int (*entry)(int, int), unsigned int timeout)
{
	int sockets[2], control[2];
	struct TestClient app;
	signal(SIGCHLD, SIG_DFL);
	alarm(timeout);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, control) == 0);
	app.pid = fork();
	assert(app.pid >= 0);
	if (app.pid == 0) {
		close(sockets[0]);
		close(control[0]);
		_exit(entry(sockets[1], control[1]));
	}
	close(sockets[1]);
	close(control[1]);
	assert(wl_client_create(dpy, sockets[0]));
	app.control = control[0];
	return app;
}

static inline void
test_client_finish(struct TestClient app)
{
	int status;
	close(app.control);
	while (!wl_list_empty(&clients)) pump();
	assert(waitpid(app.pid, &status, 0) == app.pid);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static inline void
test_cleanup(char *runtime)
{
	cleanup();
	assert(rmdir(runtime) == 0);
}

static inline Monitor *
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
#endif
