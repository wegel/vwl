#include "compositor.h"
#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>
#include "capture-result.h"

static struct capture_result
command(int fd, char cmd, uint32_t vout_id)
{
	struct capture_result result = {0};
	char reply;
	if (cmd != 'r')
		assert(write(fd, &cmd, 1) == 1);
	if (cmd == 'v')
		assert(write(fd, &vout_id, sizeof(vout_id)) == sizeof(vout_id));
	if (cmd == 's' || cmd == 'c' || cmd == 'v')
		receive(fd, &result, sizeof(result));
	receive(fd, &reply, 1);
	assert(reply == cmd);
	return result;
}

/* Force the capture client to submit a buffer allocated before the resize. */
static struct capture_result
resize_capture(int fd, Client *c, int floating)
{
	struct capture_result result;
	struct wlr_box box = c->geom;
	char ack = 'a';
	assert(write(fd, &ack, 1) == 1);
	receive(fd, &ack, 1);
	assert(ack == 'a');
	setfloating(c, floating);
	if (floating) {
		/* The default config preserves size when toggling floating. */
		box.width /= 2;
		box.height /= 2;
		resize(c, box, 0);
	}
	assert(write(fd, &ack, 1) == 1);
	receive(fd, &result, sizeof(result));
	receive(fd, &ack, 1);
	assert(ack == 'a' && result.retries == 1);
	assert(result.painted.width == client_surface(c)->current.width);
	assert(result.painted.height == client_surface(c)->current.height);
	assert(result.clean.width == result.painted.width && result.clean.height == result.painted.height);
	return result;
}

struct color_buffer {
	struct wlr_buffer base;
	uint32_t *pixels;
};
static void
color_destroy(struct wlr_buffer *base)
{
	struct color_buffer *buffer = wl_container_of(base, buffer, base);
	free(buffer->pixels);
	free(buffer);
}
static bool
color_begin(struct wlr_buffer *base, uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
	struct color_buffer *buffer = wl_container_of(base, buffer, base);
	*data = buffer->pixels;
	*format = DRM_FORMAT_ARGB8888;
	*stride = base->width * 4;
	return true;
}
static void
color_end(struct wlr_buffer *base)
{
}
static const struct wlr_buffer_impl color_impl = {
		.destroy = color_destroy,
		.begin_data_ptr_access = color_begin,
		.end_data_ptr_access = color_end,
};
static struct wlr_buffer *
color_buffer(int width, int height, uint32_t color)
{
	struct color_buffer *buffer = calloc(1, sizeof(*buffer));
	int i;
	assert(buffer);
	buffer->pixels = calloc(width * height, sizeof(uint32_t));
	assert(buffer->pixels);
	for (i = 0; i < width * height; i++) buffer->pixels[i] = color;
	wlr_buffer_init(&buffer->base, &color_impl, width, height);
	return &buffer->base;
}

static void
move_pointer(Client *c, int x, int y)
{
	int lx, ly;
	assert(wlr_scene_node_coords(&c->scene_surface->node, &lx, &ly));
	wlr_cursor_warp_closest(cursor, NULL, lx + x, ly + y);
	cursorsync();
}

int
main(void)
{
	char runtime[] = "/tmp/vwl-capture-XXXXXX";

	struct TestClient app;
	Client *c;
	Monitor *m;
	struct wlr_buffer *red, *green;
	struct capture_result result;
	struct wlr_output_state state;
	int width, height;
	struct wlr_scene_rect *popup;

	test_setup(runtime, "1", getenv("VWL_TEST_RENDERER"));
	wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_POINTER);
	app = test_client_start(capture_test_client, 30);
	command(app.control, 'r', 0);
	assert(wl_list_length(&clients) == 1);
	c = wl_container_of(clients.next, c, link);
	m = c->mon;
	setfloating(c, 1);
	resize(c, (struct wlr_box){100, 100, 220, 160}, 0);
	command(app.control, 'b', 0);
	red = color_buffer(8, 6, 0xffff0000);
	green = color_buffer(8, 6, 0xff00ff00);
	wlr_cursor_set_buffer(cursor, red, 2, 3, 1);
	move_pointer(c, 30, 40);
	result = command(app.control, 's', 0);

	width = result.painted.width;
	height = result.painted.height;
	assert(result.painted.red == 48 && result.clean.red == 0);
	assert(result.painted.x == 28 && result.painted.y == 37);
	assert(result.entered && result.x == 30 && result.y == 40);
	assert(result.hotspot_x == 2 && result.hotspot_y == 3);
	assert(result.pointer.red == 48 && result.pointer.width == 8 && result.pointer.height == 6);

	/* Both edges clip, and pointer movement alone produces new frames. */
	move_pointer(c, 0, 0);
	result = command(app.control, 'c', 0);
	assert(result.painted.width == width && result.painted.height == height);
	assert(result.painted.red == 18 && result.painted.x == 0 && result.painted.y == 0);
	move_pointer(c, width - 1, height - 1);
	result = command(app.control, 'c', 0);
	assert(result.painted.red == 12 && result.clean.red == 0);
	assert(result.painted.width == width && result.painted.height == height);
	move_pointer(c, width + 20, height + 20);
	result = command(app.control, 'c', 0);
	assert(!result.painted.red && !result.entered);

	/* Image changes without pointer movement, including a hidden cursor. */
	move_pointer(c, 30, 40);
	wlr_cursor_set_buffer(cursor, green, 2, 3, 1);
	result = command(app.control, 'c', 0);
	assert(result.painted.green == 48 && !result.painted.red && !result.clean.green);
	assert(result.pointer.green == 48);
	wlr_cursor_unset_image(cursor);
	result = command(app.control, 'c', 0);
	assert(!result.painted.green && !result.entered);

	/* Output scaling and rotation must not rotate or enlarge the shared pointer. */
	wlr_output_state_init(&state);
	wlr_output_state_set_scale(&state, 2);
	wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_90);
	assert(wlr_output_commit_state(m->wlr_output, &state));
	wlr_output_state_finish(&state);
	updatemons(NULL, NULL);
	command(app.control, 'b', 0);
	wlr_cursor_set_buffer(cursor, red, 2, 3, 1);
	move_pointer(c, 30, 40);
	result = command(app.control, 'c', 0);
	assert(result.painted.red == 48 && result.painted.x == 28 && result.painted.y == 37);
	assert(result.pointer.width == 8 && result.pointer.height == 6);

	/* A client-provided HiDPI cursor may change or rotate its buffer in place. */
	wlr_seat_pointer_notify_enter(seat, client_surface(c), 30, 40);
	command(app.control, 'p', 0);
	result = command(app.control, 'c', 0);
	assert(result.painted.red == 48 && result.pointer.width == 8 && result.pointer.height == 6);
	command(app.control, 'g', 0);
	result = command(app.control, 'c', 0);
	assert(result.painted.green == 48 && result.pointer.green == 48 && !result.clean.green);
	command(app.control, 'q', 0);
	result = command(app.control, 'c', 0);
	assert(result.painted.red == 48 && result.pointer.width == 6 && result.pointer.height == 8);

	/* Popups may extend left/up from the window; their bounds shift both
	 * content and cursor coordinates by exactly the same amount. */
	popup = wlr_scene_rect_create(&c->image_capture_scene->tree, 30, 25, (float[4]){0, 0, 0, 1});
	wlr_scene_node_set_position(&popup->node, -20, -15);
	result = command(app.control, 'c', 0);
	assert(result.x == 50 && result.y == 55);
	assert(result.painted.x == 48 && result.painted.y == 52);
	wlr_scene_node_destroy(&popup->node);

	/* Sharing a hidden window must not leak the cursor from another workspace. */
	wlr_scene_node_set_enabled(&c->scene->node, false);
	result = command(app.control, 'c', 0);
	assert(!result.entered && !result.painted.red);
	wlr_scene_node_set_enabled(&c->scene->node, true);
	wlr_cursor_set_buffer(cursor, red, 2, 3, 1);

	/* Stop/restart, and use the same capture implementation for a virtual output. */
	command(app.control, 'd', 0);
	result = command(app.control, 'v', CLIENT_VOUT(c)->id);
	assert(result.painted.red == 48 && result.clean.red == 0 && result.entered);
	command(app.control, 'd', 0);
	result = command(app.control, 's', 0);
	assert(result.painted.red == 48 && result.clean.red == 0);
	/* A portal can still submit an old-size buffer after a window resizes.
	 * It must receive a retryable error, then current pixels at the new size. */
	setfloating(c, 0);
	command(app.control, 'b', 0);
	result = command(app.control, 'c', 0);
	width = result.clean.width;
	height = result.clean.height;
	wlr_cursor_unset_image(cursor);
	popup = wlr_scene_rect_create(&c->image_capture_scene->tree, 31, 23, (float[4]){0, 1, 0, 1});
	result = resize_capture(app.control, c, 1);
	assert(result.clean.width != width || result.clean.height != height);
	assert(result.clean.green == 31 * 23 && result.painted.green == 31 * 23);
	wlr_scene_rect_set_size(popup, 19, 17);
	result = command(app.control, 'c', 0);
	assert(result.clean.green == 19 * 17 && result.painted.green == 19 * 17);
	result = resize_capture(app.control, c, 0);
	assert(result.clean.width == width && result.clean.height == height);
	assert(result.clean.green == 19 * 17 && result.painted.green == 19 * 17);
	wlr_scene_node_destroy(&popup->node);
	/* Unmapping must stop all three streams, even with an update queued. */
	move_pointer(c, 40, 40);
	command(app.control, 't', 0);
	command(app.control, 'd', 0);
	test_client_finish(app);
	wlr_buffer_drop(red);
	wlr_buffer_drop(green);
	test_cleanup(runtime);
	puts("capture: pixel, cursor mode, scaling, resize retry and lifetime checks passed");
	return 0;
}
