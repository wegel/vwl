/* Check pointer lookup through unowned and compositor-drawn scene nodes. */
#undef NDEBUG
#define main vwl_main
#include "../vwl.c"
#undef main
#include <assert.h>
#include <wlr/interfaces/wlr_buffer.h>

static void
buffer_destroy(struct wlr_buffer *buffer)
{
	free(buffer);
}
static const struct wlr_buffer_impl buffer_impl = {.destroy = buffer_destroy};

int
main(void)
{
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *rect;
	struct wlr_scene_buffer *image;
	struct wlr_buffer *buffer;
	struct wlr_surface *surface;
	Client client = {.type = XDGShell}, *found;
	LayerSurface panel = {.type = LayerShell}, *layer;
	int i;

	scene = wlr_scene_create();
	assert(scene);
	for (i = 0; i < NUM_LAYERS; i++) {
		layers[i] = wlr_scene_tree_create(&scene->tree);
		assert(layers[i]);
	}
	rect = wlr_scene_rect_create(layers[LyrBg], 20, 20, (float[4]){0, 0, 0, 1});
	assert(rect);
	/* The root has no parent or owner. UBSan detects the old null access. */
	xytonode(5, 5, &surface, &found, &layer, NULL, NULL);
	assert(!surface && !found && !layer);
	wlr_scene_node_destroy(&rect->node);

	tree = wlr_scene_tree_create(layers[LyrTop]);
	tree->node.data = &client;
	rect = wlr_scene_rect_create(tree, 20, 20, (float[4]){0, 0, 0, 1});
	assert(rect);
	xytonode(5, 5, &surface, &found, &layer, NULL, NULL);
	assert(!surface && found == &client && !layer);
	/* Return the panel which owns the node, not its parent's data. */
	tree->node.data = &panel;
	xytonode(5, 5, &surface, &found, &layer, NULL, NULL);
	assert(!surface && !found && layer == &panel);
	wlr_scene_node_destroy(&tree->node);

	/* A drawn buffer, such as a tab header, is not a Wayland surface. */
	buffer = ecalloc(1, sizeof(*buffer));
	wlr_buffer_init(buffer, &buffer_impl, 20, 20);
	image = wlr_scene_buffer_create(layers[LyrTop], buffer);
	assert(image);
	wlr_buffer_drop(buffer);
	assert(wlr_scene_node_at(&layers[LyrTop]->node, 5, 5, NULL, NULL) == &image->node);
	xytonode(5, 5, &surface, &found, &layer, NULL, NULL);
	assert(!surface && !found && !layer);
	wlr_scene_node_destroy(&scene->tree.node);
	puts("hit: unowned nodes, panels and compositor buffers passed");
	return 0;
}
