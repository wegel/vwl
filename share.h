#ifndef SHARE_H
#define SHARE_H

typedef struct Client Client;

void share_init(struct wl_display *display, struct wl_event_loop *loop, struct wlr_allocator *allocator,
		struct wlr_renderer *renderer);
void share_cleanuplisteners(void);
void share_create_toplevel(Client *c);
void share_destroy(Client *c);
void share_update_title(Client *c);

#endif
