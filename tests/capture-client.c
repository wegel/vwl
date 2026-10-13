/* Reuse the small test window; this executable also acts as a capture client. */
#include "fullscreen-client.c"
#include <sys/mman.h>
#include <limits.h>
#include "capture-result.h"
#include "capture-copy-client-protocol.h"
#include "capture-source-client-protocol.h"
#include "capture-list-client-protocol.h"
#include "capture-vout-client-protocol.h"

static struct ext_image_copy_capture_manager_v1 *copy_manager;
static struct ext_foreign_toplevel_image_capture_source_manager_v1 *source_manager;
static struct vwl_vout_image_capture_source_manager_v1 *vout_manager;
static struct ext_foreign_toplevel_handle_v1 *handle;
static struct wl_seat *capture_seat;
static struct wl_pointer *capture_pointer;
static struct capture_result result;
static uint32_t pointer_serial;
static struct wl_surface *cursor_surface;

static void
pointer_focus_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface, wl_fixed_t x,
		wl_fixed_t y)
{
	pointer_serial = serial;
}
static void
pointer_focus_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface)
{
}
static void
pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
}
static void
pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
}
static void
pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value)
{
}
static const struct wl_pointer_listener input_pointer_listener = {
		.enter = pointer_focus_enter,
		.leave = pointer_focus_leave,
		.motion = pointer_motion,
		.button = pointer_button,
		.axis = pointer_axis,
};

static void
client_cursor(char command)
{
	FILE *file = tmpfile();
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	uint32_t pixels[16 * 12];
	int i;
	for (i = 0; i < 16 * 12; i++) pixels[i] = command == 'g' ? 0xff00ff00 : 0xffff0000;
	assert(file && fwrite(pixels, sizeof(pixels), 1, file) == 1 && fflush(file) == 0);
	pool = wl_shm_create_pool(test_shm, fileno(file), sizeof(pixels));
	buffer = wl_shm_pool_create_buffer(pool, 0, 16, 12, 16 * 4, WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer, &buffer_listener, NULL);
	if (!cursor_surface)
		cursor_surface = wl_compositor_create_surface(test_compositor);
	wl_surface_set_buffer_scale(cursor_surface, 2);
	wl_surface_set_buffer_transform(
			cursor_surface, command == 'q' ? WL_OUTPUT_TRANSFORM_90 : WL_OUTPUT_TRANSFORM_NORMAL);
	wl_surface_attach(cursor_surface, buffer, 0, 0);
	wl_surface_damage(cursor_surface, 0, 0, 16, 16);
	wl_surface_commit(cursor_surface);
	assert(pointer_serial);
	wl_pointer_set_cursor(capture_pointer, pointer_serial, cursor_surface, 2, 3);
	wl_shm_pool_destroy(pool);
	fclose(file);
}

struct test_cast {
	struct ext_image_copy_capture_session_v1 *session;
	int width, height, done, stopped;
	uint32_t format;
};

static void
handle_closed(void *data, struct ext_foreign_toplevel_handle_v1 *h)
{
}
static void
handle_done(void *data, struct ext_foreign_toplevel_handle_v1 *h)
{
}
static void
handle_string(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *s)
{
}
static const struct ext_foreign_toplevel_handle_v1_listener handle_listener = {
		.closed = handle_closed,
		.done = handle_done,
		.title = handle_string,
		.app_id = handle_string,
		.identifier = handle_string,
};
static void
list_toplevel(void *data, struct ext_foreign_toplevel_list_v1 *list, struct ext_foreign_toplevel_handle_v1 *h)
{
	if (!handle)
		handle = h;
	ext_foreign_toplevel_handle_v1_add_listener(h, &handle_listener, NULL);
}
static void
list_finished(void *data, struct ext_foreign_toplevel_list_v1 *list)
{
}
static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
		.toplevel = list_toplevel,
		.finished = list_finished,
};
static void
capture_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	registry_global(data, registry, name, interface, version);
	if (!strcmp(interface, ext_image_copy_capture_manager_v1_interface.name))
		copy_manager = wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
	else if (!strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name))
		source_manager = wl_registry_bind(
				registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1);
	else if (!strcmp(interface, vwl_vout_image_capture_source_manager_v1_interface.name))
		vout_manager = wl_registry_bind(registry, name, &vwl_vout_image_capture_source_manager_v1_interface, 1);
	else if (!strcmp(interface, wl_seat_interface.name))
		capture_seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	else if (!strcmp(interface, ext_foreign_toplevel_list_v1_interface.name)) {
		struct ext_foreign_toplevel_list_v1 *list =
				wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1);
		ext_foreign_toplevel_list_v1_add_listener(list, &list_listener, NULL);
	}
}
static const struct wl_registry_listener capture_registry_listener = {
		.global = capture_global,
		.global_remove = registry_remove,
};

static void
cast_size(void *data, struct ext_image_copy_capture_session_v1 *s, uint32_t w, uint32_t h)
{
	struct test_cast *cast = data;
	cast->width = w;
	cast->height = h;
}
static void
cast_format(void *data, struct ext_image_copy_capture_session_v1 *s, uint32_t format)
{
	struct test_cast *cast = data;
	cast->format = format;
}
static void
cast_device(void *data, struct ext_image_copy_capture_session_v1 *s, struct wl_array *device)
{
}
static void
cast_dmabuf(void *data, struct ext_image_copy_capture_session_v1 *s, uint32_t fmt, struct wl_array *mods)
{
}
static void
cast_done(void *data, struct ext_image_copy_capture_session_v1 *s)
{
	((struct test_cast *)data)->done = 1;
}
static void
cast_stopped(void *data, struct ext_image_copy_capture_session_v1 *s)
{
	((struct test_cast *)data)->stopped = 1;
}
static const struct ext_image_copy_capture_session_v1_listener cast_listener = {
		.buffer_size = cast_size,
		.shm_format = cast_format,
		.dmabuf_device = cast_device,
		.dmabuf_format = cast_dmabuf,
		.done = cast_done,
		.stopped = cast_stopped,
};
static void
frame_transform(void *data, struct ext_image_copy_capture_frame_v1 *f, uint32_t transform)
{
	assert(transform == WL_OUTPUT_TRANSFORM_NORMAL);
}
static void
frame_damage(void *data, struct ext_image_copy_capture_frame_v1 *f, int32_t x, int32_t y, int32_t w, int32_t h)
{
}
static void
frame_time(void *data, struct ext_image_copy_capture_frame_v1 *f, uint32_t hi, uint32_t lo, uint32_t ns)
{
}
static void
frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *f)
{
	*(int *)data = 1;
}
static void
frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *f, uint32_t why)
{
	assert(why == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
	*(int *)data = -1;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
		.transform = frame_transform,
		.damage = frame_damage,
		.presentation_time = frame_time,
		.ready = frame_ready,
		.failed = frame_failed,
};
static void
pointer_enter(void *data, struct ext_image_copy_capture_cursor_session_v1 *p)
{
	result.entered = 1;
}
static void
pointer_leave(void *data, struct ext_image_copy_capture_cursor_session_v1 *p)
{
	result.entered = 0;
}
static void
pointer_position(void *data, struct ext_image_copy_capture_cursor_session_v1 *p, int32_t x, int32_t y)
{
	result.x = x;
	result.y = y;
}
static void
pointer_hotspot(void *data, struct ext_image_copy_capture_cursor_session_v1 *p, int32_t x, int32_t y)
{
	result.hotspot_x = x;
	result.hotspot_y = y;
}
static const struct ext_image_copy_capture_cursor_session_v1_listener pointer_listener = {
		.enter = pointer_enter,
		.leave = pointer_leave,
		.position = pointer_position,
		.hotspot = pointer_hotspot,
};

static struct capture_pixels
snapshot_once(struct wl_display *display, struct test_cast *cast, int synchronize_fd)
{
	struct capture_pixels pixels = {.width = cast->width, .height = cast->height, .x = INT_MAX, .y = INT_MAX};
	struct ext_image_copy_capture_frame_v1 *frame;
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	FILE *file = tmpfile();
	int bpp = (cast->format == WL_SHM_FORMAT_BGR888 || cast->format == WL_SHM_FORMAT_RGB888) ? 3 : 4;
	size_t size = (size_t)cast->width * cast->height * bpp;
	unsigned char *data;
	int ready = 0, x, y;
	uint32_t format = cast->format;
	char ack;

	assert(cast->done && !cast->stopped && size && file);
	assert(ftruncate(fileno(file), size) == 0);
	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
	assert(data != MAP_FAILED);
	memset(data, 0xcc, size);
	pool = wl_shm_create_pool(test_shm, fileno(file), size);
	buffer = wl_shm_pool_create_buffer(pool, 0, cast->width, cast->height, cast->width * bpp, cast->format);
	frame = ext_image_copy_capture_session_v1_create_frame(cast->session);
	ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, &ready);
	ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
	ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, cast->width, cast->height);
	if (synchronize_fd >= 0) {
		/* Keep a buffer allocated before the compositor resizes the window. */
		ack = 'a';
		assert(write(synchronize_fd, &ack, 1) == 1);
		assert(read(synchronize_fd, &ack, 1) == 1 && ack == 'a');
		for (x = 0; x < 3; x++) assert(wl_display_roundtrip(display) >= 0);
	}
	ext_image_copy_capture_frame_v1_capture(frame);
	while (!ready) assert(wl_display_dispatch(display) >= 0);
	if (ready < 0) {
		pixels.width = 0;
		goto done;
	}
	for (y = 0; y < pixels.height; y++)
		for (x = 0; x < pixels.width; x++) {
			uint32_t color = 0;
			memcpy(&color, data + (y * pixels.width + x) * bpp, bpp);
			color &= 0xffffff;
			if (format == WL_SHM_FORMAT_ABGR8888 || format == WL_SHM_FORMAT_XBGR8888 ||
					format == WL_SHM_FORMAT_BGR888)
				color = ((color & 0xff) << 16) | (color & 0xff00) | ((color >> 16) & 0xff);
			if (color == 0xff0000) {
				pixels.red++;
				if (x < pixels.x)
					pixels.x = x;
				if (y < pixels.y)
					pixels.y = y;
				if (x + 1 > pixels.right)
					pixels.right = x + 1;
				if (y + 1 > pixels.bottom)
					pixels.bottom = y + 1;
			}
			if (color == 0x00ff00)
				pixels.green++;
		}
done:
	ext_image_copy_capture_frame_v1_destroy(frame);
	wl_buffer_destroy(buffer);
	wl_shm_pool_destroy(pool);
	munmap(data, size);
	fclose(file);
	return pixels;
}

static struct capture_pixels
snapshot(struct wl_display *display, struct test_cast *cast, int synchronize_fd)
{
	struct capture_pixels pixels;
	int tries, attempts = synchronize_fd < 0 ? 1 : 2;
	for (tries = 0; tries < attempts; tries++) {
		pixels = snapshot_once(display, cast, synchronize_fd);
		if (pixels.width)
			return pixels;
		result.retries++;
		synchronize_fd = -1;
		assert(wl_display_roundtrip(display) >= 0);
	}
	abort();
}

int
capture_test_client(int socket_fd, int control_fd)
{
	struct wl_display *display = wl_display_connect_to_fd(socket_fd);
	struct wl_registry *registry = wl_display_get_registry(display);
	struct test_window primary = {0};
	struct ext_image_capture_source_v1 *source = NULL;
	struct ext_image_copy_capture_cursor_session_v1 *pointer = NULL;
	struct test_cast painted = {0}, clean = {0}, separate = {0};
	char command = 'r';
	uint32_t vout_id;
	int i;

	wl_registry_add_listener(registry, &capture_registry_listener, NULL);
	assert(wl_display_roundtrip(display) >= 0);
	assert(copy_manager && source_manager && capture_seat);
	capture_pointer = wl_seat_get_pointer(capture_seat);
	wl_pointer_add_listener(capture_pointer, &input_pointer_listener, NULL);
	create_window(&primary, 0);
	for (i = 0; i < 3; i++) assert(wl_display_roundtrip(display) >= 0);
	assert(write(control_fd, &command, 1) == 1);
	while (read(control_fd, &command, 1) == 1) {
		if (command == 'p' || command == 'g' || command == 'q') {
			assert(wl_display_roundtrip(display) >= 0);
			client_cursor(command);
		}
		if (command == 's' || command == 'v') {
			assert(handle);
			if (command == 'v') {
				assert(read(control_fd, &vout_id, sizeof(vout_id)) == sizeof(vout_id));
				source = vwl_vout_image_capture_source_manager_v1_create_source(vout_manager, vout_id);
			} else
				source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
						source_manager, handle);
			painted.session = ext_image_copy_capture_manager_v1_create_session(
					copy_manager, source, EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS);
			clean.session = ext_image_copy_capture_manager_v1_create_session(copy_manager, source, 0);
			ext_image_copy_capture_session_v1_add_listener(painted.session, &cast_listener, &painted);
			ext_image_copy_capture_session_v1_add_listener(clean.session, &cast_listener, &clean);
			pointer = ext_image_copy_capture_manager_v1_create_pointer_cursor_session(
					copy_manager, source, capture_pointer);
			ext_image_copy_capture_cursor_session_v1_add_listener(pointer, &pointer_listener, NULL);
			separate.session = ext_image_copy_capture_cursor_session_v1_get_capture_session(pointer);
			ext_image_copy_capture_session_v1_add_listener(separate.session, &cast_listener, &separate);
		}
		if (command == 'a') {
			result.retries = 0;
			result.painted = snapshot(display, &painted, control_fd);
			result.clean = snapshot(display, &clean, -1);
			result.pointer = snapshot(display, &separate, -1);
			assert(write(control_fd, &result, sizeof(result)) == sizeof(result));
		}
		if (command == 't') {
			xdg_toplevel_destroy(primary.toplevel);
			xdg_surface_destroy(primary.xdg);
			wl_surface_destroy(primary.surface);
		}
		if (command == 'd') {
			ext_image_copy_capture_session_v1_destroy(painted.session);
			ext_image_copy_capture_session_v1_destroy(clean.session);
			ext_image_copy_capture_session_v1_destroy(separate.session);
			ext_image_copy_capture_cursor_session_v1_destroy(pointer);
			ext_image_capture_source_v1_destroy(source);
			painted = clean = separate = (struct test_cast){0};
		}
		for (i = 0; i < 3; i++) assert(wl_display_roundtrip(display) >= 0);
		if (command == 'c' || command == 's' || command == 'v') {
			result.painted = snapshot(display, &painted, -1);
			result.clean = snapshot(display, &clean, -1);
			result.pointer = snapshot(display, &separate, -1);
			assert(write(control_fd, &result, sizeof(result)) == sizeof(result));
		}
		if (command == 't')
			assert(painted.stopped && clean.stopped && separate.stopped);
		assert(write(control_fd, &command, 1) == 1);
	}
	wl_display_disconnect(display);
	close(control_fd);
	return 0;
}
