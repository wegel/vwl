/* Small Wayland app used by the fullscreen compositor tests. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

struct test_window {
	struct wl_surface *surface;
	struct xdg_surface *xdg;
	struct xdg_toplevel *toplevel;
	int width, height;
};

static struct wl_compositor *test_compositor;
static struct wl_shm *test_shm;
static struct xdg_wm_base *test_shell;

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
	wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {.release = buffer_release};

static void
surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	struct test_window *window = data;
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	FILE *file = tmpfile();
	int size = window->width * window->height * 4;

	assert(file && ftruncate(fileno(file), size) == 0);
	pool = wl_shm_create_pool(test_shm, fileno(file), size);
	buffer = wl_shm_pool_create_buffer(
			pool, 0, window->width, window->height, window->width * 4, WL_SHM_FORMAT_XRGB8888);
	wl_buffer_add_listener(buffer, &buffer_listener, NULL);
	wl_shm_pool_destroy(pool);
	fclose(file);
	xdg_surface_ack_configure(surface, serial);
	wl_surface_attach(window->surface, buffer, 0, 0);
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);
	wl_surface_commit(window->surface);
}

static void
toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states)
{
	struct test_window *window = data;
	if (width > 0)
		window->width = width;
	if (height > 0)
		window->height = height;
}

static void
toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
}

static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};
static const struct xdg_toplevel_listener toplevel_listener = {
		.configure = toplevel_configure,
		.close = toplevel_close,
};

static void
shell_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener shell_listener = {.ping = shell_ping};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	if (!strcmp(interface, wl_compositor_interface.name))
		test_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	else if (!strcmp(interface, wl_shm_interface.name))
		test_shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (!strcmp(interface, xdg_wm_base_interface.name)) {
		test_shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(test_shell, &shell_listener, NULL);
	}
}

static void
registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
		.global = registry_global,
		.global_remove = registry_remove,
};

static void
create_window(struct test_window *window, int fullscreen)
{
	window->width = 320;
	window->height = 240;
	window->surface = wl_compositor_create_surface(test_compositor);
	window->xdg = xdg_wm_base_get_xdg_surface(test_shell, window->surface);
	xdg_surface_add_listener(window->xdg, &surface_listener, window);
	window->toplevel = xdg_surface_get_toplevel(window->xdg);
	xdg_toplevel_add_listener(window->toplevel, &toplevel_listener, window);
	xdg_toplevel_set_app_id(window->toplevel, "vwl-fullscreen-test");
	if (fullscreen)
		xdg_toplevel_set_fullscreen(window->toplevel, NULL);
	wl_surface_commit(window->surface);
}

int
fullscreen_test_client(int socket_fd, int control_fd)
{
	struct wl_display *display = wl_display_connect_to_fd(socket_fd);
	struct wl_registry *registry;
	struct test_window primary = {0}, sibling = {0};
	char command = 'r';
	int i;

	assert(display);
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	assert(wl_display_roundtrip(display) >= 0);
	assert(test_compositor && test_shm && test_shell);
	create_window(&primary, 1);
	do {
		switch (command) {
		case 'f':
			xdg_toplevel_set_fullscreen(primary.toplevel, NULL);
			break;
		case 'u':
			xdg_toplevel_unset_fullscreen(primary.toplevel);
			break;
		case 'n':
			create_window(&sibling, 0);
			break;
		case 'd':
			xdg_toplevel_destroy(sibling.toplevel);
			xdg_surface_destroy(sibling.xdg);
			wl_surface_destroy(sibling.surface);
			break;
		}
		/* Complete configure/ack/map exchanges before acknowledging the command. */
		for (i = 0; i < 3; i++) assert(wl_display_roundtrip(display) >= 0);
		assert(write(control_fd, &command, 1) == 1);
	} while (read(control_fd, &command, 1) == 1);
	wl_display_disconnect(display);
	close(control_fd);
	return 0;
}
