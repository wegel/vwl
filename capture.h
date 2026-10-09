#ifndef CAPTURE_H
#define CAPTURE_H

struct wl_display;
struct wl_event_loop;
struct wlr_allocator;
struct wlr_renderer;
struct wlr_ext_image_capture_source_v1;
struct Client;
struct VirtualOutput;

void capture_init(struct wl_display *display, struct wl_event_loop *loop, struct wlr_allocator *allocator,
		struct wlr_renderer *renderer);
void capture_finish(void);
/* Pass exactly one of client or vout. The source follows its owner's lifetime. */
struct wlr_ext_image_capture_source_v1 *capture_create(struct Client *client, struct VirtualOutput *vout);
void capture_update(void);

#endif
