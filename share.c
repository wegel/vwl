#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "vwl.h"
#include "share.h"
#include "vwl-vout-image-capture-source-unstable-v1-protocol.h"

static struct wlr_ext_foreign_toplevel_list_v1 *foreign_toplevel_list;
static struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *foreign_toplevel_capture_mgr;
static struct vwl_vout_image_capture_source_manager_v1 *vout_capture_mgr;
static struct wl_event_loop *share_event_loop;
static struct wlr_allocator *share_allocator;
static struct wlr_renderer *share_renderer;
static struct share_cursor {
	struct wlr_buffer *buffer;
	int32_t hotspot_x;
	int32_t hotspot_y;
	Client *last_hovered;
} share_cursor;

static bool ensureimagesource(Client *c);
static bool ensurevoutimagesource(VirtualOutput *vout);
static void imagesourcedestroy(struct wl_listener *listener, void *data);
static void voutimagesourcedestroy(struct wl_listener *listener, void *data);
static void handlenewforeigntoplevelcapturerequest(struct wl_listener *listener, void *data);

struct vwl_vout_image_capture_source_manager_v1 {
	struct wl_global *global;
	struct wl_listener display_destroy;
};

struct vout_image_source {
	struct wlr_ext_image_capture_source_v1 base;
	VirtualOutput *vout;
	struct wlr_backend backend;
	struct wlr_output output;
	struct wlr_scene_output *scene_output;
	size_t num_started;
	struct wl_listener vout_destroy;
	struct wl_listener scene_output_destroy;
	struct wl_listener output_frame;
};

struct vout_image_source_frame_event {
	struct wlr_ext_image_capture_source_v1_frame_event base;
	struct wlr_buffer *buffer;
	struct timespec when;
};

static struct wl_listener foreign_toplevel_capture_request = {
		.notify = handlenewforeigntoplevelcapturerequest,
};

static size_t last_vout_capture_num;

static void loadcursorimage(struct wlr_xcursor_manager *mgr);
static struct wlr_box voutcapturebox(VirtualOutput *vout);
static void sourceupdateconstraints(struct vout_image_source *source, const struct wlr_output_state *state);
static void rendersource(struct vout_image_source *source);
static void destroysource(struct vout_image_source *source);
static void sourcehandlevoutdestroy(struct wl_listener *listener, void *data);
static void sourcehandlesceneoutputdestroy(struct wl_listener *listener, void *data);
static void sourcehandleoutputframe(struct wl_listener *listener, void *data);
static bool outputtest(struct wlr_output *output, const struct wlr_output_state *state);
static bool outputcommit(struct wlr_output *output, const struct wlr_output_state *state);
static void sourcestart(struct wlr_ext_image_capture_source_v1 *base, bool with_cursors);
static void sourcestop(struct wlr_ext_image_capture_source_v1 *base);
static void sourcerequestframe(struct wlr_ext_image_capture_source_v1 *base, bool schedule_frame);
static void sourcecopyframe(struct wlr_ext_image_capture_source_v1 *base,
		struct wlr_ext_image_copy_capture_frame_v1 *frame,
		struct wlr_ext_image_capture_source_v1_frame_event *base_event);
static void handlevoutmanagercreatesource(
		struct wl_client *client, struct wl_resource *manager_resource, uint32_t new_id, uint32_t vout_id);
static void handlevoutmanagerdestroy(struct wl_client *client, struct wl_resource *manager_resource);
static void bindvoutmanager(struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void handlevoutmanagerdisplaydestroy(struct wl_listener *listener, void *data);

static const struct wlr_ext_image_capture_source_v1_interface vout_source_impl = {
		.start = sourcestart,
		.stop = sourcestop,
		.request_frame = sourcerequestframe,
		.copy_frame = sourcecopyframe,
};

static const struct wlr_backend_impl vout_backend_impl = {0};

static const struct wlr_output_impl vout_output_impl = {
		.test = outputtest,
		.commit = outputcommit,
};

static const struct vwl_vout_image_capture_source_manager_v1_interface vout_manager_impl = {
		.create_source = handlevoutmanagercreatesource,
		.destroy = handlevoutmanagerdestroy,
};

void
share_init(struct wl_display *display, struct wl_event_loop *loop, struct wlr_allocator *allocator,
		struct wlr_renderer *renderer)
{
	share_event_loop = loop;
	share_allocator = allocator;
	share_renderer = renderer;
	foreign_toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(display, 1);
	foreign_toplevel_capture_mgr = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(display, 1);
	if (foreign_toplevel_capture_mgr)
		wl_signal_add(&foreign_toplevel_capture_mgr->events.new_request, &foreign_toplevel_capture_request);
	vout_capture_mgr = calloc(1, sizeof(*vout_capture_mgr));
	if (!vout_capture_mgr)
		return;
	vout_capture_mgr->global = wl_global_create(display, &vwl_vout_image_capture_source_manager_v1_interface, 1,
			vout_capture_mgr, bindvoutmanager);
	if (!vout_capture_mgr->global) {
		free(vout_capture_mgr);
		vout_capture_mgr = NULL;
		return;
	}
	vout_capture_mgr->display_destroy.notify = handlevoutmanagerdisplaydestroy;
	wl_display_add_destroy_listener(display, &vout_capture_mgr->display_destroy);
}

void
share_cleanuplisteners(void)
{
	if (foreign_toplevel_capture_request.link.prev && foreign_toplevel_capture_request.link.next) {
		wl_list_remove(&foreign_toplevel_capture_request.link);
		wl_list_init(&foreign_toplevel_capture_request.link);
	}
}

static struct wlr_box
voutcapturebox(VirtualOutput *vout)
{
	if (!vout || !vout->mon)
		return (struct wlr_box){0};
	if (!wlr_box_empty(&vout->layout_geom))
		return vout->layout_geom;
	return vout->mon->window_area;
}

bool
share_create_capture_scene(Client *c)
{
	if (!c || c->image_capture_scene)
		return c && c->image_capture_scene;

	c->image_capture_scene = wlr_scene_create();
	if (!c->image_capture_scene)
		return false;

	if (client_is_x11(c)) {
		c->image_capture_tree = wlr_scene_tree_create(&c->image_capture_scene->tree);
		if (!c->image_capture_tree)
			goto fail;
		if (!wlr_scene_surface_create(c->image_capture_tree, client_surface(c)))
			goto fail;
	} else {
		c->image_capture_tree = wlr_scene_xdg_surface_create(&c->image_capture_scene->tree, c->surface.xdg);
		if (!c->image_capture_tree)
			goto fail;
	}

	loadcursorimage(cursor_mgr);
	if (share_cursor.buffer) {
		c->image_capture_cursor = wlr_scene_buffer_create(&c->image_capture_scene->tree, share_cursor.buffer);
		if (c->image_capture_cursor)
			wlr_scene_node_set_enabled(&c->image_capture_cursor->node, false);
	}

	return true;

fail:
	wlr_scene_node_destroy(&c->image_capture_scene->tree.node);
	c->image_capture_scene = NULL;
	c->image_capture_tree = NULL;
	return false;
}

void
share_create_toplevel(Client *c)
{
	struct wlr_ext_foreign_toplevel_handle_v1_state state;

	if (!c || c->ext_foreign_toplevel || !foreign_toplevel_list)
		return;

	state = (struct wlr_ext_foreign_toplevel_handle_v1_state){
			.app_id = client_get_appid(c),
			.title = client_get_title(c),
	};
	c->ext_foreign_toplevel = wlr_ext_foreign_toplevel_handle_v1_create(foreign_toplevel_list, &state);
	if (c->ext_foreign_toplevel)
		c->ext_foreign_toplevel->data = c;
}

void
share_destroy_capture_scene(Client *c)
{
	if (!c || !c->image_capture_scene)
		return;

	wlr_scene_node_destroy(&c->image_capture_scene->tree.node);
	c->image_capture_scene = NULL;
	c->image_capture_tree = NULL;
	c->image_capture_cursor = NULL;
}

void
share_destroy(Client *c)
{
	if (!c)
		return;

	if (share_cursor.last_hovered == c)
		share_cursor.last_hovered = NULL;

	if (c->image_capture_source) {
		wl_list_remove(&c->image_capture_source_destroy.link);
		wl_list_init(&c->image_capture_source_destroy.link);
		c->image_capture_source = NULL;
	}

	if (!c->ext_foreign_toplevel)
		return;

	wlr_ext_foreign_toplevel_handle_v1_destroy(c->ext_foreign_toplevel);
	c->ext_foreign_toplevel = NULL;
}

bool
share_is_captured(Client *c)
{
	return c && c->image_capture_source && !wl_list_empty(&c->image_capture_source->resources);
}

void
share_update_title(Client *c)
{
	struct wlr_ext_foreign_toplevel_handle_v1_state state;

	if (!c || !c->ext_foreign_toplevel)
		return;

	state = (struct wlr_ext_foreign_toplevel_handle_v1_state){
			.app_id = client_get_appid(c),
			.title = client_get_title(c),
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(c->ext_foreign_toplevel, &state);
}

void
share_update_capture_cursors(double lx, double ly, Client *hovered)
{
	if (hovered && (!hovered->image_capture_cursor || !share_is_captured(hovered)))
		hovered = NULL;

	if (!share_cursor.last_hovered && !hovered)
		return;

	if (share_cursor.last_hovered && share_cursor.last_hovered != hovered)
		wlr_scene_node_set_enabled(&share_cursor.last_hovered->image_capture_cursor->node, false);

	share_cursor.last_hovered = hovered;

	if (!hovered)
		return;

	wlr_scene_node_set_position(&hovered->image_capture_cursor->node,
			(int)(lx - hovered->geom.x - share_cursor.hotspot_x + hovered->bw),
			(int)(ly - hovered->geom.y - share_cursor.hotspot_y + hovered->bw));
	wlr_scene_node_set_enabled(&hovered->image_capture_cursor->node, true);
}

static void
loadcursorimage(struct wlr_xcursor_manager *mgr)
{
	struct wlr_xcursor *xcursor;
	struct wlr_xcursor_image *image;

	if (!mgr || share_cursor.buffer)
		return;
	xcursor = wlr_xcursor_manager_get_xcursor(mgr, "default", 1);
	if (!xcursor || xcursor->image_count == 0)
		return;
	image = xcursor->images[0];
	share_cursor.buffer = wlr_xcursor_image_get_buffer(image);
	share_cursor.hotspot_x = (int32_t)image->hotspot_x;
	share_cursor.hotspot_y = (int32_t)image->hotspot_y;
}

static void
sourceupdateconstraints(struct vout_image_source *source, const struct wlr_output_state *state)
{
	struct wlr_output *output = &source->output;

	if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain))
		return;

	wlr_ext_image_capture_source_v1_set_constraints_from_swapchain(
			&source->base, output->swapchain, output->renderer);
}

static void
rendersource(struct vout_image_source *source)
{
	struct wlr_box box;
	struct wlr_output_state state;
	pixman_region32_t full_damage;
	bool ok;

	if (!source || !source->scene_output || !source->vout)
		return;

	box = voutcapturebox(source->vout);
	if (box.width <= 0 || box.height <= 0)
		return;

	wlr_scene_output_set_position(source->scene_output, box.x, box.y);

	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	wlr_output_state_set_custom_mode(&state, box.width, box.height, 0);
	ok = wlr_scene_output_build_state(source->scene_output, &state, NULL);
	if (ok) {
		pixman_region32_init_rect(&full_damage, 0, 0, box.width, box.height);
		if (!(state.committed & WLR_OUTPUT_STATE_DAMAGE) || pixman_region32_empty(&state.damage))
			wlr_output_state_set_damage(&state, &full_damage);
		ok = wlr_output_commit_state(source->scene_output->output, &state);
		pixman_region32_fini(&full_damage);
	}
	wlr_output_state_finish(&state);
	if (!ok)
		wlr_log(WLR_ERROR, "failed to render vout capture source");
}

static void
sourcestart(struct wlr_ext_image_capture_source_v1 *base, bool with_cursors)
{
	struct vout_image_source *source = wl_container_of(base, source, base);
	struct timespec now;
	(void)with_cursors;

	source->num_started++;
	if (source->num_started > 1)
		return;

	rendersource(source);
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (source->scene_output)
		wlr_scene_output_send_frame_done(source->scene_output, &now);
}

static void
sourcestop(struct wlr_ext_image_capture_source_v1 *base)
{
	struct vout_image_source *source = wl_container_of(base, source, base);
	struct wlr_output_state state;

	if (source->num_started == 0)
		return;
	source->num_started--;
	if (source->num_started > 0)
		return;

	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, false);
	wlr_output_commit_state(&source->output, &state);
	wlr_output_state_finish(&state);
}

static void
sourcerequestframe(struct wlr_ext_image_capture_source_v1 *base, bool schedule_frame)
{
	struct vout_image_source *source = wl_container_of(base, source, base);
	struct timespec now;

	if (source->output.frame_pending)
		wlr_output_send_frame(&source->output);
	if (schedule_frame)
		wlr_output_update_needs_frame(&source->output);
	if (source->output.frame_pending)
		return;
	if (source->num_started == 0 || !source->scene_output)
		return;

	rendersource(source);
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(source->scene_output, &now);
}

static void
sourcecopyframe(struct wlr_ext_image_capture_source_v1 *base, struct wlr_ext_image_copy_capture_frame_v1 *frame,
		struct wlr_ext_image_capture_source_v1_frame_event *base_event)
{
	struct vout_image_source *source = wl_container_of(base, source, base);
	struct vout_image_source_frame_event *event = wl_container_of(base_event, event, base);

	if (wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, event->buffer, source->output.renderer))
		wlr_ext_image_copy_capture_frame_v1_ready(frame, source->output.transform, &event->when);
}

static bool
outputtest(struct wlr_output *output, const struct wlr_output_state *state)
{
	uint32_t supported = WLR_OUTPUT_STATE_BACKEND_OPTIONAL | WLR_OUTPUT_STATE_BUFFER | WLR_OUTPUT_STATE_ENABLED |
			     WLR_OUTPUT_STATE_MODE;

	if ((state->committed & ~supported) != 0)
		return false;

	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		if (state->buffer == NULL)
			return false;
		if (state->mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM &&
				(state->buffer->width != state->custom_mode.width ||
						state->buffer->height != state->custom_mode.height))
			return false;
		if (state->buffer_src_box.x != 0.0 || state->buffer_src_box.y != 0.0)
			return false;
		if ((state->buffer_src_box.width != 0.0 &&
				    state->buffer_src_box.width != (double)state->buffer->width) ||
				(state->buffer_src_box.height != 0.0 &&
						state->buffer_src_box.height != (double)state->buffer->height))
			return false;
	}

	return output != NULL;
}

static bool
outputcommit(struct wlr_output *output, const struct wlr_output_state *state)
{
	struct vout_image_source *source = wl_container_of(output, source, output);
	struct wlr_buffer *buffer;
	struct timespec now;
	struct vout_image_source_frame_event frame_event;
	pixman_region32_t full_damage;
	const pixman_region32_t *damage;

	if ((state->committed & WLR_OUTPUT_STATE_ENABLED) && !state->enabled)
		return true;

	if (state->committed & WLR_OUTPUT_STATE_MODE)
		sourceupdateconstraints(source, state);

	if (!(state->committed & WLR_OUTPUT_STATE_BUFFER)) {
		wlr_log(WLR_DEBUG, "failed to commit vout capture output: missing buffer");
		return false;
	}

	buffer = state->buffer;
	pixman_region32_init_rect(&full_damage, 0, 0, buffer->width, buffer->height);
	damage = (state->committed & WLR_OUTPUT_STATE_DAMAGE) ? &state->damage : &full_damage;

	clock_gettime(CLOCK_MONOTONIC, &now);
	frame_event = (struct vout_image_source_frame_event){
			.base = {.damage = damage},
			.buffer = buffer,
			.when = now,
	};
	wl_signal_emit_mutable(&source->base.events.frame, &frame_event.base);
	pixman_region32_fini(&full_damage);
	return true;
}

static void
destroysource(struct vout_image_source *source)
{
	if (!source)
		return;

	if (source->vout_destroy.link.prev && source->vout_destroy.link.next) {
		wl_list_remove(&source->vout_destroy.link);
		wl_list_init(&source->vout_destroy.link);
	}
	if (source->scene_output_destroy.link.prev && source->scene_output_destroy.link.next) {
		wl_list_remove(&source->scene_output_destroy.link);
		wl_list_init(&source->scene_output_destroy.link);
	}
	if (source->output_frame.link.prev && source->output_frame.link.next) {
		wl_list_remove(&source->output_frame.link);
		wl_list_init(&source->output_frame.link);
	}
	wlr_ext_image_capture_source_v1_finish(&source->base);
	if (source->scene_output)
		wlr_scene_output_destroy(source->scene_output);
	wlr_output_finish(&source->output);
	wlr_backend_finish(&source->backend);
	free(source);
}

static void
sourcehandlevoutdestroy(struct wl_listener *listener, void *data)
{
	struct vout_image_source *source = wl_container_of(listener, source, vout_destroy);
	(void)data;

	destroysource(source);
}

static void
sourcehandlesceneoutputdestroy(struct wl_listener *listener, void *data)
{
	struct vout_image_source *source = wl_container_of(listener, source, scene_output_destroy);
	(void)data;

	source->scene_output = NULL;
	wl_list_remove(&source->scene_output_destroy.link);
	wl_list_init(&source->scene_output_destroy.link);
}

static void
sourcehandleoutputframe(struct wl_listener *listener, void *data)
{
	struct vout_image_source *source = wl_container_of(listener, source, output_frame);
	struct timespec now;
	(void)data;

	if (!source->scene_output)
		return;
	if (!wlr_scene_output_needs_frame(source->scene_output))
		return;

	rendersource(source);

	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(source->scene_output, &now);
}

static bool
ensurevoutimagesource(VirtualOutput *vout)
{
	struct vout_image_source *source;
	char name[64];

	if (!vout || !vout->mon)
		return false;
	if (vout->image_capture_source)
		return true;

	source = calloc(1, sizeof(*source));
	if (!source)
		return false;

	source->vout = vout;
	wlr_ext_image_capture_source_v1_init(&source->base, &vout_source_impl);
	wlr_backend_init(&source->backend, &vout_backend_impl);
	source->backend.buffer_caps = WLR_BUFFER_CAP_DMABUF | WLR_BUFFER_CAP_SHM;

	wlr_output_init(&source->output, &source->backend, &vout_output_impl, share_event_loop, NULL);
	snprintf(name, sizeof(name), "VOUT-CAPTURE-%zu", ++last_vout_capture_num);
	wlr_output_set_name(&source->output, name);
	if (!wlr_output_init_render(&source->output, share_allocator, share_renderer)) {
		wlr_output_finish(&source->output);
		wlr_backend_finish(&source->backend);
		free(source);
		return false;
	}

	source->scene_output = wlr_scene_output_create(scene, &source->output);
	if (!source->scene_output) {
		wlr_output_finish(&source->output);
		wlr_backend_finish(&source->backend);
		free(source);
		return false;
	}

	source->vout_destroy.notify = sourcehandlevoutdestroy;
	wl_signal_add(&vout->events.destroy, &source->vout_destroy);
	source->scene_output_destroy.notify = sourcehandlesceneoutputdestroy;
	wl_signal_add(&source->scene_output->events.destroy, &source->scene_output_destroy);
	source->output_frame.notify = sourcehandleoutputframe;
	wl_signal_add(&source->output.events.frame, &source->output_frame);

	vout->image_capture_source = &source->base;
	LISTEN(&source->base.events.destroy, &vout->image_capture_source_destroy, voutimagesourcedestroy);
	return true;
}

static bool
ensureimagesource(Client *c)
{
	if (!c || c->image_capture_source)
		return c && c->image_capture_source;
	if (!c->image_capture_scene && !share_create_capture_scene(c))
		return false;

	c->image_capture_source = wlr_ext_image_capture_source_v1_create_with_scene_node(
			&c->image_capture_scene->tree.node, share_event_loop, share_allocator, share_renderer);
	if (!c->image_capture_source)
		return false;

	LISTEN(&c->image_capture_source->events.destroy, &c->image_capture_source_destroy, imagesourcedestroy);
	return true;
}

static void
imagesourcedestroy(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, image_capture_source_destroy);
	(void)data;

	wl_list_remove(&c->image_capture_source_destroy.link);
	wl_list_init(&c->image_capture_source_destroy.link);
	c->image_capture_source = NULL;
}

static void
voutimagesourcedestroy(struct wl_listener *listener, void *data)
{
	VirtualOutput *vout = wl_container_of(listener, vout, image_capture_source_destroy);
	(void)data;

	wl_list_remove(&vout->image_capture_source_destroy.link);
	wl_list_init(&vout->image_capture_source_destroy.link);
	vout->image_capture_source = NULL;
}

static void
handlenewforeigntoplevelcapturerequest(struct wl_listener *listener, void *data)
{
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
	Client *c;
	(void)listener;

	c = request->toplevel_handle ? request->toplevel_handle->data : NULL;
	if (!c || !c->image_capture_scene || !client_surface(c)->mapped)
		return;
	if (!ensureimagesource(c))
		return;
	if (!wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, c->image_capture_source))
		wlr_log(WLR_ERROR, "failed to accept foreign toplevel capture request");
}

static void
handlevoutmanagercreatesource(
		struct wl_client *client, struct wl_resource *manager_resource, uint32_t new_id, uint32_t vout_id)
{
	VirtualOutput *vout = voutbyid(vout_id);

	if (!vout) {
		wlr_ext_image_capture_source_v1_create_resource(NULL, client, new_id);
		return;
	}
	if (!ensurevoutimagesource(vout)) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wlr_ext_image_capture_source_v1_create_resource(vout->image_capture_source, client, new_id);
}

static void
handlevoutmanagerdestroy(struct wl_client *client, struct wl_resource *manager_resource)
{
	(void)client;
	wl_resource_destroy(manager_resource);
}

static void
bindvoutmanager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &vwl_vout_image_capture_source_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &vout_manager_impl, data, NULL);
}

static void
handlevoutmanagerdisplaydestroy(struct wl_listener *listener, void *data)
{
	struct vwl_vout_image_capture_source_manager_v1 *manager = wl_container_of(listener, manager, display_destroy);
	(void)data;

	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	if (vout_capture_mgr == manager)
		vout_capture_mgr = NULL;
	free(manager);
}
