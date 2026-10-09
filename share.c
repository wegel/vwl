#include <stdbool.h>
#include <stdlib.h>

#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/util/log.h>

#include "vwl.h"
#include "share.h"
#include "capture.h"
#include "vwl-vout-image-capture-source-unstable-v1-protocol.h"

static struct wlr_ext_foreign_toplevel_list_v1 *foreign_toplevel_list;
static struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *foreign_toplevel_capture_mgr;
static struct vwl_vout_image_capture_source_manager_v1 *vout_capture_mgr;

static bool ensureimagesource(Client *c);
static bool ensurevoutimagesource(VirtualOutput *vout);
static void imagesourcedestroy(struct wl_listener *listener, void *data);
static void voutimagesourcedestroy(struct wl_listener *listener, void *data);
static void handlenewforeigntoplevelcapturerequest(struct wl_listener *listener, void *data);

struct vwl_vout_image_capture_source_manager_v1 {
	struct wl_global *global;
	struct wl_listener display_destroy;
};

static struct wl_listener foreign_toplevel_capture_request = {
		.notify = handlenewforeigntoplevelcapturerequest,
};

static void handlevoutmanagercreatesource(
		struct wl_client *client, struct wl_resource *manager_resource, uint32_t new_id, uint32_t vout_id);
static void handlevoutmanagerdestroy(struct wl_client *client, struct wl_resource *manager_resource);
static void bindvoutmanager(struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void handlevoutmanagerdisplaydestroy(struct wl_listener *listener, void *data);

static const struct vwl_vout_image_capture_source_manager_v1_interface vout_manager_impl = {
		.create_source = handlevoutmanagercreatesource,
		.destroy = handlevoutmanagerdestroy,
};

void
share_init(struct wl_display *display, struct wl_event_loop *loop, struct wlr_allocator *allocator,
		struct wlr_renderer *renderer)
{
	capture_init(display, loop, allocator, renderer);
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
	capture_finish();
	if (foreign_toplevel_capture_request.link.prev && foreign_toplevel_capture_request.link.next) {
		wl_list_remove(&foreign_toplevel_capture_request.link);
		wl_list_init(&foreign_toplevel_capture_request.link);
	}
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
		return true;
	}

	c->image_capture_tree = wlr_scene_xdg_surface_create(&c->image_capture_scene->tree, c->surface.xdg);
	if (!c->image_capture_tree)
		goto fail;

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
}

void
share_destroy(Client *c)
{
	if (!c)
		return;

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

static bool
ensurevoutimagesource(VirtualOutput *vout)
{
	if (!vout || !vout->mon)
		return false;
	if (vout->image_capture_source)
		return true;
	vout->image_capture_source = capture_create(NULL, vout);
	if (!vout->image_capture_source)
		return false;
	LISTEN(&vout->image_capture_source->events.destroy, &vout->image_capture_source_destroy,
			voutimagesourcedestroy);
	return true;
}

static bool
ensureimagesource(Client *c)
{
	if (!c || c->image_capture_source)
		return c && c->image_capture_source;
	if (!c->image_capture_scene && !share_create_capture_scene(c))
		return false;

	c->image_capture_source = capture_create(c, NULL);
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
