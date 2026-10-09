/* Render content first, then the real cursor, without changing capture bounds.
 * Only public wlroots interfaces are used; each session keeps its cursor mode. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <drm_fourcc.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/transform.h>
#include "vwl.h"
#include "capture.h"

struct capture_session {
	struct wl_list link;
	struct wlr_ext_image_copy_capture_session_v1 *session;
	bool paint_cursor;
	struct wl_listener destroy;
};

struct capture_source {
	struct wlr_ext_image_capture_source_v1 base;
	struct wlr_ext_image_capture_source_v1_cursor pointer;
	struct wl_list link, sessions;
	Client *client;
	VirtualOutput *vout;
	struct wlr_backend backend;
	struct wlr_output output;
	struct wlr_scene_output *scene_output;
	struct wlr_swapchain *paint_swapchain, *pointer_swapchain;
	struct wlr_box box;
	struct wl_event_source *idle;
	size_t started;
	bool pending_paint, rendering, destroying;
	struct wlr_output *watched_output;
	struct wl_listener owner_destroy, scene_output_destroy, output_frame;
	struct wl_listener cursor_damage, cursor_commit, cursor_needs_frame, cursor_output_destroy;
};

struct capture_frame {
	struct wlr_ext_image_capture_source_v1_frame_event base;
	struct wlr_buffer *buffer;
	struct timespec when;
};

static struct wl_list sources = {&sources, &sources};
static struct wl_event_loop *capture_loop;
static struct wlr_allocator *capture_allocator;
static struct wlr_renderer *capture_renderer;
static struct wl_listener new_session;
static size_t capture_serial;
static void schedule(struct capture_source *source);
static void render(struct capture_source *source);
static void destroy(struct capture_source *source);
static void watch_cursor(struct capture_source *source);
static const struct wlr_ext_image_capture_source_v1_interface source_impl;

/* The cursor is drawn later and cannot enlarge these content bounds. */
static void
node_extents(struct wlr_scene_node *node, int x, int y, pixman_region32_t *region)
{
	struct wlr_scene_node *child;
	struct wlr_scene_buffer *buffer;
	struct wlr_scene_rect *rect;
	int width = 0, height = 0;

	if (!node->enabled)
		return;
	x += node->x;
	y += node->y;
	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link)
			node_extents(child, x, y, region);
		return;
	case WLR_SCENE_NODE_RECT:
		rect = wlr_scene_rect_from_node(node);
		width = rect->width;
		height = rect->height;
		break;
	case WLR_SCENE_NODE_BUFFER:
		buffer = wlr_scene_buffer_from_node(node);
		width = buffer->dst_width;
		height = buffer->dst_height;
		if (!width && buffer->buffer) {
			width = buffer->transform & 1 ? buffer->buffer->height : buffer->buffer->width;
			height = buffer->transform & 1 ? buffer->buffer->width : buffer->buffer->height;
		}
		break;
	}
	if (width > 0 && height > 0)
		pixman_region32_union_rect(region, region, x, y, width, height);
}

static struct wlr_box
capture_box(struct capture_source *source)
{
	pixman_region32_t region;
	pixman_box32_t *box;
	struct wlr_box result;

	if (source->vout) {
		result = source->vout->layout_geom;
		if (result.width <= 0 || result.height <= 0)
			result = source->vout->mon->window_area;
		return result;
	}
	pixman_region32_init(&region);
	node_extents(&source->client->image_capture_scene->tree.node, 0, 0, &region);
	box = pixman_region32_extents(&region);
	result = (struct wlr_box){box->x1, box->y1, box->x2 - box->x1, box->y2 - box->y1};
	pixman_region32_fini(&region);
	return result;
}

/* Read wlroots' real image, including animated themes and client cursors.
 * vwl has one pointer. Its texture is borrowed only for this render pass. */
static struct wlr_output_cursor *
pointer_image(struct capture_source *source, struct wlr_box *dst)
{
	struct wlr_output_cursor *image;
	Monitor *mon;
	Client *under = NULL;
	int lx = 0, ly = 0, cx = 0, cy = 0;
	double x, y, scale;

	source->pointer.entered = false;
	if (!cursor || !seat || locked || !(mon = xytomon(cursor->x, cursor->y)))
		return NULL;
	x = cursor->x;
	y = cursor->y;
	if (source->client) {
		xytonode(x, y, NULL, &under, NULL, NULL, NULL);
		if (under != source->client || !source->client->scene_surface ||
				!wlr_scene_node_coords(&source->client->scene_surface->node, &lx, &ly))
			return NULL;
		wlr_scene_node_coords(&source->client->image_capture_tree->node, &cx, &cy);
		x += cx - lx;
		y += cy - ly;
	}
	if (!wlr_box_contains_point(&source->box, x, y))
		return NULL;
	scale = mon->wlr_output->scale;
	wl_list_for_each(image, &mon->wlr_output->cursors, link) {
		if (!image->enabled || !image->visible || !image->texture)
			continue;
		source->pointer.entered = true;
		source->pointer.x = (int)round(x - source->box.x);
		source->pointer.y = (int)round(y - source->box.y);
		source->pointer.hotspot.x = (int)round(image->hotspot_x / scale);
		source->pointer.hotspot.y = (int)round(image->hotspot_y / scale);
		*dst = (struct wlr_box){
				.x = source->pointer.x - source->pointer.hotspot.x,
				.y = source->pointer.y - source->pointer.hotspot.y,
				.width = MAX(1, (int)round(image->width / scale)),
				.height = MAX(1, (int)round(image->height / scale)),
		};
		return image;
	}
	return NULL;
}

static struct wlr_buffer *
acquire(struct capture_source *source, struct wlr_swapchain **swapchain, int width, int height, uint32_t format)
{
	struct wlr_output_state state;
	bool ok;

	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	wlr_output_state_set_custom_mode(&state, width, height, 0);
	wlr_output_state_set_render_format(&state, format);
	ok = wlr_output_configure_primary_swapchain(&source->output, &state, swapchain);
	wlr_output_state_finish(&state);
	return ok ? wlr_swapchain_acquire(*swapchain) : NULL;
}

/* Use a separate buffer so simultaneous sessions cannot alter each other.
 * The fixed render target clips the pointer at window edges. */
static struct wlr_buffer *
paint(struct capture_source *source, struct wlr_buffer *content, struct wlr_output_cursor *image, struct wlr_box dst)
{
	struct wlr_swapchain **swapchain = content ? &source->paint_swapchain : &source->pointer_swapchain;
	int width = content ? content->width : MAX(1, dst.width);
	int height = content ? content->height : MAX(1, dst.height);
	/* Keep painted frames in the same format as the clean frame: GLES readback
	 * formats advertised for RGB buffers need not work on RGBA buffers. */
	uint32_t format = content ? source->output.swapchain->format.format : DRM_FORMAT_ARGB8888;
	struct wlr_buffer *buffer = acquire(source, swapchain, width, height, format);
	struct wlr_texture *texture = content ? wlr_texture_from_buffer(capture_renderer, content) : NULL;
	struct wlr_render_pass *pass;
	bool ok;

	if (!buffer || (content && !texture))
		goto fail;
	pass = wlr_renderer_begin_buffer_pass(capture_renderer, buffer, NULL);
	if (!pass)
		goto fail;
	if (texture) {
		wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = texture,
								  .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
	} else {
		wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = {0, 0, width, height},
							       .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
		dst.x = dst.y = 0;
	}
	if (image) {
		wlr_render_pass_add_texture(
				pass, &(struct wlr_render_texture_options){
						      .texture = image->texture,
						      .src_box = image->src_box,
						      .dst_box = dst,
						      .transform = wlr_output_transform_invert(image->transform),
						      .wait_timeline = image->wait_timeline,
						      .wait_point = image->wait_point,
				      });
	}
	ok = wlr_render_pass_submit(pass);
	if (texture)
		wlr_texture_destroy(texture);
	if (ok)
		return buffer;
	wlr_buffer_unlock(buffer);
	return NULL;
fail:
	if (texture)
		wlr_texture_destroy(texture);
	wlr_buffer_unlock(buffer);
	return NULL;
}

static void
send_frame(struct wlr_ext_image_capture_source_v1 *base, struct wlr_buffer *buffer)
{
	pixman_region32_t damage;
	struct capture_frame event = {.buffer = buffer};

	clock_gettime(CLOCK_MONOTONIC, &event.when);
	/* Also erase the old pointer position in reused client buffers. */
	pixman_region32_init_rect(&damage, 0, 0, buffer->width, buffer->height);
	event.base.damage = &damage;
	wl_signal_emit_mutable(&base->events.frame, &event.base);
	pixman_region32_fini(&damage);
}

static void
copy_frame(struct wlr_ext_image_capture_source_v1 *base, struct wlr_ext_image_copy_capture_frame_v1 *frame,
		struct wlr_ext_image_capture_source_v1_frame_event *event_base)
{
	struct capture_source *source = wl_container_of(base, source, base);
	struct capture_frame *event = wl_container_of(event_base, event, base);
	struct capture_session *cast;
	struct wlr_buffer *buffer = event->buffer, *painted = NULL;
	struct wlr_output_cursor *image;
	struct wlr_box dst = {0};

	wl_list_for_each(cast, &source->sessions, link) {
		if (cast->session->frame != frame)
			continue;
		if (cast->paint_cursor && (image = pointer_image(source, &dst))) {
			painted = paint(source, buffer, image, dst);
			if (!painted) {
				wlr_ext_image_copy_capture_frame_v1_fail(
						frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
				return;
			}
			buffer = painted;
		}
		break;
	}
	if (wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, buffer, capture_renderer))
		wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &event->when);
	wlr_buffer_unlock(painted);
}

static void
pointer_copy_frame(struct wlr_ext_image_capture_source_v1 *base, struct wlr_ext_image_copy_capture_frame_v1 *frame,
		struct wlr_ext_image_capture_source_v1_frame_event *event_base)
{
	struct capture_frame *event = wl_container_of(event_base, event, base);

	if (wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, event->buffer, capture_renderer))
		wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &event->when);
}

static void
update_pointer(struct capture_source *source)
{
	struct wlr_box dst = {0};
	struct wlr_output_cursor *image = pointer_image(source, &dst);
	struct wlr_buffer *buffer = paint(source, NULL, image, dst);
	struct wlr_ext_image_capture_source_v1 *base = &source->pointer.base;

	if (!buffer)
		return;
	if (base->width != (uint32_t)buffer->width || base->height != (uint32_t)buffer->height)
		wlr_ext_image_capture_source_v1_set_constraints_from_swapchain(
				base, source->pointer_swapchain, capture_renderer);
	wl_signal_emit_mutable(&source->pointer.events.update, NULL);
	send_frame(base, buffer);
	wlr_buffer_unlock(buffer);
}

static void
idle_render(void *data)
{
	struct capture_source *source = data;
	source->idle = NULL;
	if (source->started) {
		render(source);
	} else {
		/* A pointer session can request positions before requesting images. */
		watch_cursor(source);
		source->box = capture_box(source);
		update_pointer(source);
	}
}

static void
schedule(struct capture_source *source)
{
	if ((source->started || !wl_list_empty(&source->pointer.events.update.listener_list)) && !source->idle &&
			!source->rendering && !source->destroying)
		source->idle = wl_event_loop_add_idle(capture_loop, idle_render, source);
}

static void
render(struct capture_source *source)
{
	struct wlr_output_state state;
	struct timespec now;
	bool ok;

	if (!source->scene_output || source->rendering || source->destroying)
		return;
	source->box = capture_box(source);
	if (source->box.width <= 0 || source->box.height <= 0)
		return;
	source->rendering = true;
	watch_cursor(source);
	if (source->output.frame_pending)
		wlr_output_send_frame(&source->output);
	wlr_scene_output_set_position(source->scene_output, source->box.x, source->box.y);
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	wlr_output_state_set_custom_mode(&state, source->box.width, source->box.height, 0);
	ok = wlr_scene_output_build_state(source->scene_output, &state, NULL) &&
	     wlr_output_commit_state(&source->output, &state);
	wlr_output_state_finish(&state);
	if (!wl_list_empty(&source->pointer.events.update.listener_list) ||
			!wl_list_empty(&source->pointer.base.events.frame.listener_list))
		update_pointer(source);
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(source->scene_output, &now);
	source->rendering = false;
	if (!ok)
		wlr_log(WLR_ERROR, "failed to render capture source");
}

static void
start(struct wlr_ext_image_capture_source_v1 *base, bool with_cursors)
{
	struct capture_source *source = wl_container_of(base, source, base);
	source->pending_paint = with_cursors;
	source->started++;
	/* Publish sizes/formats before wlroots announces the new session. */
	render(source);
}

static void
stop_source(struct capture_source *source)
{
	struct wlr_output_state state;

	assert(source->started > 0);
	if (--source->started || source->destroying)
		return;
	if (source->idle) {
		wl_event_source_remove(source->idle);
		source->idle = NULL;
	}
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, false);
	wlr_output_commit_state(&source->output, &state);
	wlr_output_state_finish(&state);
}

static void
stop(struct wlr_ext_image_capture_source_v1 *base)
{
	struct capture_source *source = wl_container_of(base, source, base);
	stop_source(source);
}

static void
request_frame(struct wlr_ext_image_capture_source_v1 *base, bool schedule_frame)
{
	struct capture_source *source = wl_container_of(base, source, base);
	if (schedule_frame)
		schedule(source);
}

static void
pointer_start(struct wlr_ext_image_capture_source_v1 *base, bool with_cursors)
{
	struct capture_source *source = wl_container_of(base, source, pointer.base);
	source->started++;
	render(source);
}

static void
pointer_stop(struct wlr_ext_image_capture_source_v1 *base)
{
	struct capture_source *source = wl_container_of(base, source, pointer.base);
	stop_source(source);
}

static void
pointer_request_frame(struct wlr_ext_image_capture_source_v1 *base, bool schedule_frame)
{
	struct capture_source *source = wl_container_of(base, source, pointer.base);
	if (schedule_frame)
		schedule(source);
}

static struct wlr_ext_image_capture_source_v1_cursor *
get_pointer(struct wlr_ext_image_capture_source_v1 *base, struct wlr_seat *requested_seat)
{
	struct capture_source *source = wl_container_of(base, source, base);
	if (requested_seat != seat)
		return NULL;
	source->box = capture_box(source);
	watch_cursor(source);
	update_pointer(source);
	return &source->pointer;
}

static const struct wlr_ext_image_capture_source_v1_interface source_impl = {
		.start = start,
		.stop = stop,
		.request_frame = request_frame,
		.copy_frame = copy_frame,
		.get_pointer_cursor = get_pointer,
};
static const struct wlr_ext_image_capture_source_v1_interface pointer_impl = {
		.start = pointer_start,
		.stop = pointer_stop,
		.request_frame = pointer_request_frame,
		.copy_frame = pointer_copy_frame,
};

static bool
output_commit(struct wlr_output *output, const struct wlr_output_state *state)
{
	struct capture_source *source = wl_container_of(output, source, output);

	if ((state->committed & WLR_OUTPUT_STATE_ENABLED) && !state->enabled)
		return true;
	if (!state->buffer)
		return false;
	if (source->base.width != (uint32_t)state->buffer->width ||
			source->base.height != (uint32_t)state->buffer->height) {
		if (!wlr_ext_image_capture_source_v1_set_constraints_from_swapchain(
				    &source->base, output->swapchain, capture_renderer))
			return false;
	}
	send_frame(&source->base, state->buffer);
	return true;
}

static const struct wlr_backend_impl backend_impl = {0};
static const struct wlr_output_impl output_impl = {.commit = output_commit};

static void
session_destroy(struct wl_listener *listener, void *data)
{
	struct capture_session *cast = wl_container_of(listener, cast, destroy);
	wl_list_remove(&cast->destroy.link);
	wl_list_remove(&cast->link);
	free(cast);
}

static void
session_created(struct wl_listener *listener, void *data)
{
	struct wlr_ext_image_copy_capture_session_v1 *cast = data;
	struct capture_source *source;
	struct capture_session *tracked;

	if (cast->source->impl != &source_impl)
		return;
	source = wl_container_of(cast->source, source, base);
	tracked = calloc(1, sizeof(*tracked));
	if (!tracked) {
		wl_resource_post_no_memory(cast->resource);
		return;
	}
	tracked->session = cast;
	/* start() runs immediately before new_session. No private session fields
	 * or callbacks are read or replaced. */
	tracked->paint_cursor = source->pending_paint;
	wl_list_insert(&source->sessions, &tracked->link);
	LISTEN(&cast->events.destroy, &tracked->destroy, session_destroy);
}

static void
unwatch_cursor(struct capture_source *source)
{
	if (!source->watched_output)
		return;
	wl_list_remove(&source->cursor_damage.link);
	wl_list_remove(&source->cursor_commit.link);
	wl_list_remove(&source->cursor_needs_frame.link);
	wl_list_remove(&source->cursor_output_destroy.link);
	source->watched_output = NULL;
}

static void
cursor_damage(struct wl_listener *listener, void *data)
{
	struct capture_source *source = wl_container_of(listener, source, cursor_damage);
	schedule(source);
}

static void
cursor_commit(struct wl_listener *listener, void *data)
{
	struct capture_source *source = wl_container_of(listener, source, cursor_commit);
	schedule(source);
}

static void
cursor_needs_frame(struct wl_listener *listener, void *data)
{
	struct capture_source *source = wl_container_of(listener, source, cursor_needs_frame);
	schedule(source);
}

static void
cursor_output_destroy(struct wl_listener *listener, void *data)
{
	struct capture_source *source = wl_container_of(listener, source, cursor_output_destroy);
	unwatch_cursor(source);
	schedule(source);
}

static void
watch_cursor(struct capture_source *source)
{
	Monitor *mon = cursor ? xytomon(cursor->x, cursor->y) : NULL;
	struct wlr_output *output = mon ? mon->wlr_output : NULL;

	if (source->watched_output == output)
		return;
	unwatch_cursor(source);
	if (!output)
		return;
	source->watched_output = output;
	LISTEN(&output->events.damage, &source->cursor_damage, cursor_damage);
	LISTEN(&output->events.commit, &source->cursor_commit, cursor_commit);
	LISTEN(&output->events.needs_frame, &source->cursor_needs_frame, cursor_needs_frame);
	LISTEN(&output->events.destroy, &source->cursor_output_destroy, cursor_output_destroy);
}

void
capture_update(void)
{
	struct capture_source *source;
	wl_list_for_each(source, &sources, link) schedule(source);
}

static void
output_frame(struct wl_listener *listener, void *data)
{
	struct capture_source *source = wl_container_of(listener, source, output_frame);
	schedule(source);
}

static void
scene_output_destroy(struct wl_listener *listener, void *data)
{
	struct capture_source *source = wl_container_of(listener, source, scene_output_destroy);
	source->scene_output = NULL;
	wl_list_remove(&source->scene_output_destroy.link);
	wl_list_init(&source->scene_output_destroy.link);
}

static void
owner_destroy(struct wl_listener *listener, void *data)
{
	struct capture_source *source = wl_container_of(listener, source, owner_destroy);
	destroy(source);
}

static void
destroy(struct capture_source *source)
{
	source->destroying = true;
	if (source->idle)
		wl_event_source_remove(source->idle);
	unwatch_cursor(source);
	wl_list_remove(&source->link);
	wl_list_remove(&source->owner_destroy.link);
	wl_list_remove(&source->scene_output_destroy.link);
	wl_list_remove(&source->output_frame.link);
	wlr_ext_image_capture_source_v1_finish(&source->base);
	wlr_ext_image_capture_source_v1_cursor_finish(&source->pointer);
	if (source->scene_output)
		wlr_scene_output_destroy(source->scene_output);
	wlr_swapchain_destroy(source->paint_swapchain);
	wlr_swapchain_destroy(source->pointer_swapchain);
	wlr_output_finish(&source->output);
	wlr_backend_finish(&source->backend);
	free(source);
}

struct wlr_ext_image_capture_source_v1 *
capture_create(Client *client, VirtualOutput *vout)
{
	struct capture_source *source = calloc(1, sizeof(*source));
	struct wlr_scene *content = client ? client->image_capture_scene : scene;
	char name[64];

	assert(!!client != !!vout);
	if (!source)
		return NULL;
	source->client = client;
	source->vout = vout;
	wl_list_init(&source->sessions);
	wlr_ext_image_capture_source_v1_init(&source->base, &source_impl);
	wlr_ext_image_capture_source_v1_cursor_init(&source->pointer, &pointer_impl);
	wlr_backend_init(&source->backend, &backend_impl);
	source->backend.buffer_caps = WLR_BUFFER_CAP_DMABUF | WLR_BUFFER_CAP_SHM;
	wlr_output_init(&source->output, &source->backend, &output_impl, capture_loop, NULL);
	snprintf(name, sizeof(name), "VWL-CAPTURE-%zu", ++capture_serial);
	wlr_output_set_name(&source->output, name);
	if (!wlr_output_init_render(&source->output, capture_allocator, capture_renderer))
		goto fail;
	source->scene_output = wlr_scene_output_create(content, &source->output);
	if (!source->scene_output)
		goto fail;
	wlr_output_lock_attach_render(&source->output, true);
	wl_list_insert(&sources, &source->link);
	LISTEN(client ? &content->tree.node.events.destroy : &vout->events.destroy, &source->owner_destroy,
			owner_destroy);
	LISTEN(&source->scene_output->events.destroy, &source->scene_output_destroy, scene_output_destroy);
	LISTEN(&source->output.events.needs_frame, &source->output_frame, output_frame);
	return &source->base;
fail:
	wlr_ext_image_capture_source_v1_finish(&source->base);
	wlr_ext_image_capture_source_v1_cursor_finish(&source->pointer);
	wlr_output_finish(&source->output);
	wlr_backend_finish(&source->backend);
	free(source);
	return NULL;
}

void
capture_init(struct wl_display *display, struct wl_event_loop *loop, struct wlr_allocator *allocator,
		struct wlr_renderer *renderer)
{
	struct wlr_ext_image_copy_capture_manager_v1 *manager =
			wlr_ext_image_copy_capture_manager_v1_create(display, 1);
	capture_loop = loop;
	capture_allocator = allocator;
	capture_renderer = renderer;
	if (!manager) {
		wlr_log(WLR_ERROR, "failed to create capture manager");
		return;
	}
	LISTEN(&manager->events.new_session, &new_session, session_created);
}

void
capture_finish(void)
{
	struct capture_source *source, *tmp;
	wl_list_for_each_safe(source, tmp, &sources, link) destroy(source);
	if (new_session.link.next) {
		wl_list_remove(&new_session.link);
		wl_list_init(&new_session.link);
	}
}
