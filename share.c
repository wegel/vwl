#include <stdbool.h>

#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/util/log.h>

#include "vwl.h"
#include "share.h"

static struct wlr_ext_foreign_toplevel_list_v1 *foreign_toplevel_list;
static struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *foreign_toplevel_capture_mgr;
static struct wl_event_loop *share_event_loop;
static struct wlr_allocator *share_allocator;
static struct wlr_renderer *share_renderer;

static bool ensureimagesource(Client *c);
static void imagesourcedestroy(struct wl_listener *listener, void *data);
static void handlenewforeigntoplevelcapturerequest(struct wl_listener *listener, void *data);

static struct wl_listener foreign_toplevel_capture_request = {
		.notify = handlenewforeigntoplevelcapturerequest,
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
}

void
share_cleanuplisteners(void)
{
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
