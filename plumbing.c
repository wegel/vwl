/*
 * plumbing.c - Infrastructure and protocol handling for vwl compositor
 *
 * This file contains all the "plumbing" functions that handle Wayland
 * protocol interactions, event listeners, device management, and other
 * infrastructure that is not specific to window management features.
 */

#define _POSIX_C_SOURCE 200809L
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <wlr/backend/libinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#endif

#include "vwl.h"
#include "util.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "vwl-ipc-unstable-v1-protocol.h"
#pragma GCC diagnostic pop

/* Forward declarations needed by config.h */
void tile(Monitor *m);
void tabbed(Monitor *m);
void *ecalloc(size_t nmemb, size_t size);
void spawn(const Arg *arg);
void killclient(const Arg *arg);
void focusstack(const Arg *arg);
void tabmove(const Arg *arg);
void setmfact(const Arg *arg);
void zoom(const Arg *arg);
void togglefullscreen(const Arg *arg);
void toggletabbed(const Arg *arg);
void togglefloating(const Arg *arg);
void setlayout(const Arg *arg);
void moveresize(const Arg *arg);
void focusmon(const Arg *arg);
void tagmon(const Arg *arg);
void moveworkspace(const Arg *arg);
void view(const Arg *arg);
void tag(const Arg *arg);
void chvt(const Arg *arg);
void debugstate(const Arg *arg);

/* Forward declarations for functions needed from vwl.c */
VirtualOutput *focusvout(Monitor *m);

/* Forward declarations for IPC functions */
void ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
void ipc_manager_destroy(struct wl_resource *resource);
void ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *output);
void ipc_manager_release(struct wl_client *client, struct wl_resource *resource);
void ipc_output_destroy(struct wl_resource *resource);
void ipc_output_printstatus(Monitor *monitor);
void ipc_output_printstatus_to(IPCOutput *ipc_output);
void ipc_output_release(struct wl_client *client, struct wl_resource *resource);
void updateipc(void);

#include "config.h"

/* Static data */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };

/* Global variable definitions that are shared between vwl.c and plumbing.c */
struct wlr_pointer_constraint_v1 *active_constraint;
struct wlr_scene_rect *locked_bg;
struct wlr_session_lock_v1 *cur_lock;
struct wl_display *dpy;
struct wl_event_loop *event_loop;
struct wlr_backend *backend;
struct wlr_scene *scene;
struct wlr_scene_tree *layers[NUM_LAYERS];
struct wlr_renderer *drw;
struct wlr_allocator *alloc;
struct wlr_compositor *compositor;
struct wlr_session *session;
struct wlr_xdg_shell *xdg_shell;
struct wlr_seat *seat;
struct wlr_cursor *cursor;
struct wlr_xcursor_manager *cursor_mgr;
struct wlr_output_layout *output_layout;
struct wlr_scene_tree *drag_icon;
struct wl_list clients;
struct wl_list fstack;
struct wl_list mons;
Monitor *selmon;
Workspace workspaces[WORKSPACE_COUNT];
Workspace *selws;
VirtualOutput *selvout;
KeyboardGroup *kb_group;
unsigned int cursor_mode;
Client *grabc;
int grabcx, grabcy;
int locked;
void *exclusive_focus;
CursorPhysical cursor_phys;
pid_t child_pid = -1;
struct wlr_idle_notifier_v1 *idle_notifier;
struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static int fullscreen_idle_active;
#ifdef XWAYLAND
struct wlr_xwayland *xwayland;
#endif

/* Forward declarations for functions needed from vwl.c */
Monitor *xytomon(double x, double y);
void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
void focusclient(Client *c, int lift);
Client *focustop(Monitor *m);
VirtualOutput *voutat(Monitor *m, double lx, double ly);
void setworkspace(Client *c, Workspace *ws);
int client_is_unmanaged(Client *c);
int client_wants_focus(Client *c);
void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
void createkeyboard(struct wlr_keyboard *keyboard);
void createpointer(struct wlr_pointer *pointer);
KeyboardGroup *createkeyboardgroup(void);
void destroyidleinhibitor(struct wl_listener *listener, void *data);
void update_fullscreen_idle_inhibit(void);
void checkidleinhibitor(struct wlr_surface *exclude);
void destroydragicon(struct wl_listener *listener, void *data);
void destroypointerconstraint(struct wl_listener *listener, void *data);
void commitpopup(struct wl_listener *listener, void *data);
void commitlayersurfacenotify(struct wl_listener *listener, void *data);
void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
void destroylayersurfacenotify(struct wl_listener *listener, void *data);
void destroylocksurface(struct wl_listener *listener, void *data);
void requestdecorationmode(struct wl_listener *listener, void *data);
void destroydecoration(struct wl_listener *listener, void *data);
void cursorwarptohint(void);
void destroysessionlock(struct wl_listener *listener, void *data);
void unlocksession(struct wl_listener *listener, void *data);

/* Forward declarations for event handler functions (currently in vwl.c, will be moved later) */
void gpureset(struct wl_listener *listener, void *data);
void updatemons(struct wl_listener *listener, void *data);
void createidleinhibitor(struct wl_listener *listener, void *data);
void inputdevice(struct wl_listener *listener, void *data);
void virtualkeyboard(struct wl_listener *listener, void *data);
void virtualpointer(struct wl_listener *listener, void *data);
void createpointerconstraint(struct wl_listener *listener, void *data);
void createmon(struct wl_listener *listener, void *data);
void createnotify(struct wl_listener *listener, void *data);
void createpopup(struct wl_listener *listener, void *data);
void createdecoration(struct wl_listener *listener, void *data);
void createlayersurface(struct wl_listener *listener, void *data);
void outputmgrapply(struct wl_listener *listener, void *data);
void outputmgrtest(struct wl_listener *listener, void *data);
void powermgrsetmode(struct wl_listener *listener, void *data);
void urgent(struct wl_listener *listener, void *data);
void setcursor(struct wl_listener *listener, void *data);
void setpsel(struct wl_listener *listener, void *data);
void setsel(struct wl_listener *listener, void *data);
void setcursorshape(struct wl_listener *listener, void *data);
void requeststartdrag(struct wl_listener *listener, void *data);
void startdrag(struct wl_listener *listener, void *data);
void locksession(struct wl_listener *listener, void *data);
#ifdef XWAYLAND
void createnotifyx11(struct wl_listener *listener, void *data);
void xwaylandready(struct wl_listener *listener, void *data);
#endif

/* Global event handlers */
struct wl_listener cursor_axis = {.notify = axisnotify};
struct wl_listener cursor_button = {.notify = buttonpress};
struct wl_listener cursor_frame = {.notify = cursorframe};
struct wl_listener cursor_motion = {.notify = motionrelative};
struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
struct wl_listener gpu_reset = {.notify = gpureset};
struct wl_listener layout_change = {.notify = updatemons};
struct wl_listener new_idle_inhibitor = {.notify = createidleinhibitor};
struct wl_listener new_input_device = {.notify = inputdevice};
struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
struct wl_listener new_virtual_pointer = {.notify = virtualpointer};
struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
struct wl_listener new_output = {.notify = createmon};
struct wl_listener new_xdg_toplevel = {.notify = createnotify};
struct wl_listener new_xdg_popup = {.notify = createpopup};
struct wl_listener new_xdg_decoration = {.notify = createdecoration};
struct wl_listener new_layer_surface = {.notify = createlayersurface};
struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
struct wl_listener output_mgr_test = {.notify = outputmgrtest};
struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
struct wl_listener request_activate = {.notify = urgent};
struct wl_listener request_cursor = {.notify = setcursor};
struct wl_listener request_set_psel = {.notify = setpsel};
struct wl_listener request_set_sel = {.notify = setsel};
struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
struct wl_listener request_start_drag = {.notify = requeststartdrag};
struct wl_listener start_drag = {.notify = startdrag};
struct wl_listener new_session_lock = {.notify = locksession};
#ifdef XWAYLAND
struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
struct wl_listener xwayland_ready = {.notify = xwaylandready};
#endif

/* Function implementations */
void
handlesig(int signo)
{
	if (signo == SIGCHLD)
		while (waitpid(-1, NULL, WNOHANG) > 0);
	else if (signo == SIGINT || signo == SIGTERM)
		quit(NULL);
	else if (signo == SIGUSR1)
		debugstate(NULL);
}

void
cleanuplisteners(void)
{
	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
	wl_list_remove(&gpu_reset.link);
	wl_list_remove(&new_idle_inhibitor.link);
	wl_list_remove(&layout_change.link);
	wl_list_remove(&new_input_device.link);
	wl_list_remove(&new_virtual_keyboard.link);
	wl_list_remove(&new_virtual_pointer.link);
	wl_list_remove(&new_pointer_constraint.link);
	wl_list_remove(&new_output.link);
	wl_list_remove(&new_xdg_toplevel.link);
	wl_list_remove(&new_xdg_decoration.link);
	wl_list_remove(&new_xdg_popup.link);
	wl_list_remove(&new_layer_surface.link);
	wl_list_remove(&output_mgr_apply.link);
	wl_list_remove(&output_mgr_test.link);
	wl_list_remove(&output_power_mgr_set_mode.link);
	wl_list_remove(&request_activate.link);
	wl_list_remove(&request_cursor.link);
	wl_list_remove(&request_set_psel.link);
	wl_list_remove(&request_set_sel.link);
	wl_list_remove(&request_set_cursor_shape.link);
	wl_list_remove(&request_start_drag.link);
	wl_list_remove(&start_drag.link);
	wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
	wl_list_remove(&new_xwayland_surface.link);
	wl_list_remove(&xwayland_ready.link);
#endif
}

void
cleanup(void)
{
	cleanuplisteners();
#ifdef XWAYLAND
	wlr_xwayland_destroy(xwayland);
	xwayland = NULL;
#endif
	wl_display_destroy_clients(dpy);
	if (child_pid > 0) {
		kill(-child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	wlr_xcursor_manager_destroy(cursor_mgr);

	destroykeyboardgroup(&kb_group->destroy, NULL);

	/* If it's not destroyed manually, it will cause a use-after-free of wlr_seat.
	 * Destroy it until it's fixed on the wlroots side */
	wlr_backend_destroy(backend);

	wl_display_destroy(dpy);
	/* Destroy after the wayland display (when the monitors are already destroyed)
	   to avoid destroying them with an invalid scene output. */
	wlr_scene_node_destroy(&scene->tree.node);
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_pointer_axis_event *event = data;
	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	/* TODO: allow usage of scroll wheel for mousebindings, it can be implemented
	 * by checking the event's orientation and the delta of the event */
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		cursor_mode = CurPressed;
		selmon = xytomon(cursor->x, cursor->y);
		if (selmon) {
			VirtualOutput *hover_vout = voutat(selmon, cursor->x, cursor->y);
			if (hover_vout) {
				selmon->focus_vout = hover_vout;
				selvout = hover_vout;
				if (hover_vout->ws)
					selws = hover_vout->ws;
			}
		}
		if (locked)
			break;

		/* Change focus if the button was _pressed_ over a client */
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
					event->button == b->button && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* TODO: should reset to the pointer focus's current setcursor */
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			if (selmon) {
				VirtualOutput *hover_vout = voutat(selmon, cursor->x, cursor->y);
				if (hover_vout) {
					selmon->focus_vout = hover_vout;
					selvout = hover_vout;
					if (hover_vout->ws)
						setworkspace(grabc, hover_vout->ws);
				}
			}
			grabc = NULL;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. Also, some hardware emits these events. */
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec) /* this is 0 with virtual pointers */
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
gpureset(struct wl_listener *listener, void *data)
{
	struct wlr_renderer *old_drw = drw;
	struct wlr_allocator *old_alloc = alloc;
	struct Monitor *m;
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't recreate renderer");

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't recreate allocator");

	wl_list_remove(&gpu_reset.link);
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_compositor_set_renderer(compositor, drw);

	wl_list_for_each(m, &mons, link) {
		wlr_output_init_render(m->wlr_output, alloc, drw);
	}

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
	default:
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

void
createidleinhibitor(struct wl_listener *listener, void *data)
{
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
	LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

	checkidleinhibitor(NULL);
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	/* virtual keyboards shouldn't share keyboard group */
	KeyboardGroup *group = createkeyboardgroup();
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

void
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image, we will
	 * restore it after "grabbing" sending a leave event, followed by a enter
	 * event, which will result in the client requesting set the cursor surface */
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided surface as the cursor image. It will set the
	 * hardware cursor on the output that it's currently on and continue to
	 * do so as the cursor moves between outputs. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided cursor shape. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
setsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
createpopup(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client (either xdg-shell or layer-shell)
	 * creates a new popup. */
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	const Key *k;
	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod)
				&& sym == k->keysym && k->func) {
			k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

void
keypress(struct wl_listener *listener, void *data)
{
	int i;
	/* This event is raised when a key is pressed or released. */
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			group->wlr_group->keyboard.xkb_state, keycode, &syms);

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	/* On _press_ if there is no active screen locker,
	 * attempt to process a compositor keybinding. */
	if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (i = 0; i < nsyms; i++)
			handled = keybinding(mods, syms[i]) || handled;
	}

	if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
		group->mods = mods;
		group->keysyms = syms;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	} else {
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled)
		return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Pass unhandled keycodes along to the client. */
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i]);

	return 0;
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	/*
	 * Called when a client such as wlr-randr requests a change in output
	 * configuration. This is only one way that the layout can be changed,
	 * so any Monitor information should be updated by updatemons() after an
	 * output_layout.change event, not here.
	 */
	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;
		struct wlr_output_state state;

		/* Ensure displays previously disabled by wlr-output-power-management-v1
		 * are properly handled*/
		m->asleep = 0;

		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, config_head->state.enabled);
		if (!config_head->state.enabled)
			goto apply_or_test;

		if (config_head->state.mode)
			wlr_output_state_set_mode(&state, config_head->state.mode);
		else
			wlr_output_state_set_custom_mode(&state,
					config_head->state.custom_mode.width,
					config_head->state.custom_mode.height,
					config_head->state.custom_mode.refresh);

		wlr_output_state_set_transform(&state, config_head->state.transform);
		wlr_output_state_set_scale(&state, config_head->state.scale);
		wlr_output_state_set_adaptive_sync_enabled(&state,
				config_head->state.adaptive_sync_enabled);

apply_or_test:
		ok &= test ? wlr_output_test_state(wlr_output, &state)
				: wlr_output_commit_state(wlr_output, &state);

		if (!test && ok && wlr_output->enabled)
			wlr_xcursor_manager_load(cursor_mgr, wlr_output->scale);

		/* Don't move monitors if position wouldn't change. This avoids
		 * wlroots marking the output as manually configured.
		 * wlr_output_layout_add does not like disabled outputs */
		if (!test && wlr_output->enabled && (m->monitor_area.x != config_head->state.x || m->monitor_area.y != config_head->state.y))
			wlr_output_layout_add(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);

		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	/* https://codeberg.org/dwl/dwl/issues/577 */
	updatemons(NULL, NULL);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;
	/* focus follows mouse: update keyboard focus when hovering a different client */
	if (surface != seat->keyboard_state.focused_surface &&
			sloppyfocus && time && c && !client_is_unmanaged(c))
		focusclient(c, 0);
	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}
	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}
	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
rendermon(struct wl_listener *listener, void *data)
{
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	struct wlr_output_state pending = {0};
	struct timespec now;

	/* Render if no XDG clients have an outstanding resize and are visible on
	 * this monitor. */
	wl_list_for_each(c, &clients, link) {
		if (c->resize && !c->isfloating && client_is_rendered_on_mon(c, m) && !client_is_stopped(c))
			goto skip;
	}

	wlr_scene_output_commit(m->scene_output, NULL);

skip:
	/* Let clients know a frame has been rendered */
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	wlr_output_state_finish(&pending);
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output) {
		layer_surface->output = selmon ? selmon->wlr_output : NULL;
		if (!layer_surface->output) {
			wlr_layer_surface_v1_destroy(layer_surface);
			return;
		}
	}

	l = layer_surface->data = ecalloc(1, sizeof(*l));
	l->type = LayerShell;
	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	l->layer_surface = layer_surface;
	l->mon = layer_surface->output->data;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer
			< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
	l->scene->node.data = l->popups->node.data = l;

	wl_list_insert(&l->mon->layers[layer_surface->pending.layer],&l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

void
createlocksurface(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	Monitor *m = lock_surface->output->data;
	struct wlr_scene_tree *scene_tree = lock_surface->surface->data
			= wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
	m->lock_surface = lock_surface;

	wlr_scene_node_set_position(&scene_tree->node, m->monitor_area.x, m->monitor_area.y);
	wlr_session_lock_surface_v1_configure(lock_surface, m->monitor_area.width, m->monitor_area.height);

	LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

	if (m == selmon)
		client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

void
powermgrsetmode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state = {0};
	Monitor *m = event->output->data;

	if (!m)
		return;

	m->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the ouput */
	wlr_output_state_set_enabled(&state, event->mode);
	wlr_output_commit_state(m->wlr_output, &state);

	m->asleep = !event->mode;
	updatemons(NULL, NULL);
}

void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

	requestdecorationmode(&c->set_decoration_mode, deco);
}

void
destroydragicon(struct wl_listener *listener, void *data)
{
	/* Focus enter isn't sent during drag, so refocus the focused node. */
	focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	/* `data` is the wlr_surface of the idle inhibitor being destroyed,
	 * at this point the idle inhibitor is still in the list of the manager */
	checkidleinhibitor(wlr_surface_get_root_surface(data));
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
locksession(struct wl_listener *listener, void *data)
{
	struct wlr_session_lock_v1 *session_lock = data;
	SessionLock *lock;
	wlr_scene_node_set_enabled(&locked_bg->node, 1);
	if (cur_lock) {
		wlr_session_lock_v1_destroy(session_lock);
		return;
	}
	lock = session_lock->data = ecalloc(1, sizeof(*lock));
	focusclient(NULL, 0);

	lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
	cur_lock = lock->lock = session_lock;
	locked = 1;

	LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
	LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
	LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

	wlr_session_lock_v1_send_locked(session_lock);
}

void
createkeyboard(struct wlr_keyboard *keyboard)
{
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

void
createpointer(struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {

		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
			libinput_device_config_tap_set_button_map(device, button_map);
		}

		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, disable_while_typing);

		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, left_handed);

		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, scroll_method);

		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, click_method);

		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, send_events_mode);

		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, accel_profile);
			libinput_device_config_accel_set_speed(device, accel_speed);
		}
	}

	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void
commitpopup(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit)
		return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;
	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}
	box = type == LayerShell ? l->mon->monitor_area : c->mon->window_area;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	wl_list_remove(&listener->link);
	free(listener);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
		cursorsync();
	}
}

void
cursorsync(void)
{
	Monitor *m;
	double rel_x, rel_y;
	double mm_x, mm_y;
	double scale_x, scale_y;

	if (!cursor)
		return;

	m = xytomon(cursor->x, cursor->y);
	cursor_phys.mon = m;

	if (!m) {
		return;
	}

	cursor_phys.last_mon = m;
	rel_x = cursor->x - m->monitor_area.x;
	rel_y = cursor->y - m->monitor_area.y;

	scale_x = (m->phys.mm_per_px_x > 0) ? m->phys.mm_per_px_x : 1.0;
	scale_y = (m->phys.mm_per_px_y > 0) ? m->phys.mm_per_px_y : 1.0;
	mm_x = m->phys.origin_x_mm + rel_x * scale_x;
	mm_y = m->phys.origin_y_mm + rel_y * scale_y;

	cursor_phys.x_mm = mm_x;
	cursor_phys.y_mm = mm_y;
}

void
cursorinteg(double dx, double dy)
{
	Monitor *basis;
	double scale_x = 1.0;
	double scale_y = 1.0;

	basis = cursor_phys.mon ? cursor_phys.mon : cursor_phys.last_mon;
	if (basis) {
		if (basis->phys.mm_per_px_x > 0)
			scale_x = basis->phys.mm_per_px_x;
		if (basis->phys.mm_per_px_y > 0)
			scale_y = basis->phys.mm_per_px_y;
		if (!cursor_phys.last_mon)
			cursor_phys.last_mon = basis;
	}

	cursor_phys.x_mm += dx * scale_x;
	cursor_phys.y_mm += dy * scale_y;
}

int
cursorgap(Monitor *origin, double prev_mm_x, double prev_mm_y)
{
	Monitor *mon;
	double origin_left_mm, origin_right_mm, origin_top_mm, origin_bottom_mm;
	double anchor_mm;
	double best_distance = 1e18;
	Monitor *best = NULL;
	int dir = -1;
	const double tolerance_mm = 0.05;
	double target_left_mm = 0.0, target_right_mm = 0.0;
	double target_top_mm = 0.0, target_bottom_mm = 0.0;
	double scale_x = 1.0, scale_y = 1.0;
	double margin_x = 0.0, margin_y = 0.0;
	double new_mm_x, new_mm_y;
	double rel_mm_x, rel_mm_y;
	double target_x, target_y;
	double min_x, max_x, min_y, max_y;
	double attempt_mm = 0.0;

	if (!origin || origin->phys.width_mm <= 0 || origin->phys.height_mm <= 0)
		return 0;

	origin_left_mm = origin->phys.origin_x_mm;
	origin_right_mm = origin_left_mm + origin->phys.width_mm;
	origin_top_mm = origin->phys.origin_y_mm;
	origin_bottom_mm = origin_top_mm + origin->phys.height_mm;

	if (cursor_phys.x_mm < origin_left_mm - tolerance_mm)
		dir = WLR_DIRECTION_LEFT;
	else if (cursor_phys.x_mm > origin_right_mm + tolerance_mm)
		dir = WLR_DIRECTION_RIGHT;
	else if (cursor_phys.y_mm < origin_top_mm - tolerance_mm)
		dir = WLR_DIRECTION_UP;
	else if (cursor_phys.y_mm > origin_bottom_mm + tolerance_mm)
		dir = WLR_DIRECTION_DOWN;
	else
		return 0;

	if (prev_mm_x < origin_left_mm - tolerance_mm || prev_mm_x > origin_right_mm + tolerance_mm
			|| prev_mm_y < origin_top_mm - tolerance_mm || prev_mm_y > origin_bottom_mm + tolerance_mm)
		return 0;

	anchor_mm = (dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_RIGHT)
			? cursor_phys.y_mm : cursor_phys.x_mm;

	switch (dir) {
	case WLR_DIRECTION_RIGHT:
		attempt_mm = cursor_phys.x_mm - origin_right_mm;
		break;
	case WLR_DIRECTION_LEFT:
		attempt_mm = origin_left_mm - cursor_phys.x_mm;
		break;
	case WLR_DIRECTION_DOWN:
		attempt_mm = cursor_phys.y_mm - origin_bottom_mm;
		break;
	case WLR_DIRECTION_UP:
		attempt_mm = origin_top_mm - cursor_phys.y_mm;
		break;
	}

	if (attempt_mm <= tolerance_mm) {
		return 0;
	}

	wl_list_for_each(mon, &mons, link) {
		double cand_left_mm, cand_right_mm, cand_top_mm, cand_bottom_mm, distance;
		if (mon == origin || !mon->wlr_output || !mon->wlr_output->enabled)
			continue;
		if (mon->phys.width_mm <= 0 || mon->phys.height_mm <= 0)
			continue;

		cand_left_mm = mon->phys.origin_x_mm;
		cand_right_mm = cand_left_mm + mon->phys.width_mm;
		cand_top_mm = mon->phys.origin_y_mm;
		cand_bottom_mm = cand_top_mm + mon->phys.height_mm;

		switch (dir) {
		case WLR_DIRECTION_RIGHT:
			if (cand_left_mm < origin_right_mm)
				continue;
			if (anchor_mm < cand_top_mm - tolerance_mm || anchor_mm > cand_bottom_mm + tolerance_mm)
				continue;
			distance = cand_left_mm - origin_right_mm;
			break;
		case WLR_DIRECTION_LEFT:
			if (cand_right_mm > origin_left_mm)
				continue;
			if (anchor_mm < cand_top_mm - tolerance_mm || anchor_mm > cand_bottom_mm + tolerance_mm)
				continue;
			distance = origin_left_mm - cand_right_mm;
			break;
		case WLR_DIRECTION_DOWN:
			if (cand_top_mm < origin_bottom_mm)
				continue;
			if (anchor_mm < cand_left_mm - tolerance_mm || anchor_mm > cand_right_mm + tolerance_mm)
				continue;
			distance = cand_top_mm - origin_bottom_mm;
			break;
		case WLR_DIRECTION_UP:
			if (cand_bottom_mm > origin_top_mm)
				continue;
			if (anchor_mm < cand_left_mm - tolerance_mm || anchor_mm > cand_right_mm + tolerance_mm)
				continue;
			distance = origin_top_mm - cand_bottom_mm;
			break;
		default:
			continue;
		}

		if (distance < -tolerance_mm)
			continue;
		if (distance < best_distance) {
			best_distance = distance;
			best = mon;
		}
	}

	if (!best) {
		wlr_log(WLR_DEBUG, "cursor gap: no candidate from origin=%s", origin->wlr_output ? origin->wlr_output->name : "(null)");
		return 0;
	}

	target_left_mm = best->phys.origin_x_mm;
	target_right_mm = target_left_mm + best->phys.width_mm;
	target_top_mm = best->phys.origin_y_mm;
	target_bottom_mm = target_top_mm + best->phys.height_mm;
	scale_x = (best->phys.mm_per_px_x > 0) ? best->phys.mm_per_px_x : 1.0;
	scale_y = (best->phys.mm_per_px_y > 0) ? best->phys.mm_per_px_y : 1.0;
	margin_x = scale_x;
	margin_y = scale_y;
	new_mm_x = cursor_phys.x_mm;
	new_mm_y = cursor_phys.y_mm;

	if (margin_x > best->phys.width_mm / 2)
		margin_x = best->phys.width_mm / 2;
	if (margin_y > best->phys.height_mm / 2)
		margin_y = best->phys.height_mm / 2;

	if (dir == WLR_DIRECTION_RIGHT || dir == WLR_DIRECTION_LEFT) {
		if (new_mm_y < target_top_mm + margin_y)
			new_mm_y = target_top_mm + margin_y;
		if (new_mm_y > target_bottom_mm - margin_y)
			new_mm_y = target_bottom_mm - margin_y;
		if (dir == WLR_DIRECTION_RIGHT) {
			if (new_mm_x < target_left_mm + margin_x)
				new_mm_x = target_left_mm + margin_x;
			if (new_mm_x > target_right_mm - margin_x)
				new_mm_x = target_right_mm - margin_x;
		} else {
			if (new_mm_x > target_right_mm - margin_x)
				new_mm_x = target_right_mm - margin_x;
			if (new_mm_x < target_left_mm + margin_x)
				new_mm_x = target_left_mm + margin_x;
		}
	} else {
		if (new_mm_x < target_left_mm + margin_x)
			new_mm_x = target_left_mm + margin_x;
		if (new_mm_x > target_right_mm - margin_x)
			new_mm_x = target_right_mm - margin_x;
		if (dir == WLR_DIRECTION_DOWN) {
			if (new_mm_y < target_top_mm + margin_y)
				new_mm_y = target_top_mm + margin_y;
			if (new_mm_y > target_bottom_mm - margin_y)
				new_mm_y = target_bottom_mm - margin_y;
		} else {
			if (new_mm_y > target_bottom_mm - margin_y)
				new_mm_y = target_bottom_mm - margin_y;
			if (new_mm_y < target_top_mm + margin_y)
				new_mm_y = target_top_mm + margin_y;
		}
	}

	rel_mm_x = new_mm_x - target_left_mm;
	rel_mm_y = new_mm_y - target_top_mm;
	target_x = best->monitor_area.x + rel_mm_x / scale_x;
	target_y = best->monitor_area.y + rel_mm_y / scale_y;
	min_x = best->monitor_area.x;
	max_x = best->monitor_area.x + best->monitor_area.width - 1;
	min_y = best->monitor_area.y;
	max_y = best->monitor_area.y + best->monitor_area.height - 1;

	if (target_x < min_x)
		target_x = min_x;
	else if (target_x > max_x)
		target_x = max_x;
	if (target_y < min_y)
		target_y = min_y;
	else if (target_y > max_y)
		target_y = max_y;

	cursor_phys.x_mm = new_mm_x;
	cursor_phys.y_mm = new_mm_y;
	cursor_phys.mon = best;
	cursor_phys.last_mon = best;

	wlr_cursor_warp(cursor, NULL, target_x, target_y);
	cursorsync();
	motionnotify(0, NULL, 0, 0, 0, 0);
	return 1;
}

void
destroylock(SessionLock *lock, int unlock)
{
	wlr_seat_keyboard_notify_clear_focus(seat);
	if ((locked = !unlock))
		goto destroy;

	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	focusclient(focustop(selmon), 0);
	motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	wlr_scene_node_destroy(&lock->scene->node);
	cur_lock = NULL;
	free(lock);
}

void
destroysessionlock(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, destroy);
	destroylock(lock, 0);
}

void
unlocksession(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, unlock);
	destroylock(lock, 1);
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

		/* Temporarily set the layer's current state to pending
		 * so that we can easily arrange it */
		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		arrangelayers(l->mon);
		l->layer_surface->current = old_state;
		return;
	}

	if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
		return;
	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer
				< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
	}

	arrangelayers(l->mon);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, destroy);

	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	wlr_scene_node_destroy(&l->scene->node);
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);

	l->mapped = 0;
	wlr_scene_node_set_enabled(&l->scene->node, 0);
	if (l == exclusive_focus)
		exclusive_focus = NULL;
	if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
		arrangelayers(l->mon);
	if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
		focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->monitor_area;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

		if (!layer_surface->initialized)
			continue;

		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;

		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->monitor_area;
	struct wlr_box old_area = m->window_area;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	if (!m->wlr_output->enabled)
		return;

	/* Arrange exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	m->window_area = usable_area;
	arrangevout(m, &usable_area);
	if (!wlr_box_equal(&usable_area, &old_area))
		arrange(m);

	/* Arrange non-exlusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	/* Find topmost keyboard interactive layer, if such a layer exists */
	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
				continue;
			/* Deactivate the focused client. */
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}

void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
destroylocksurface(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
	struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

	m->lock_surface = NULL;
	wl_list_remove(&m->destroy_lock_surface.link);

	if (lock_surface->surface != seat->keyboard_state.focused_surface)
		return;

	if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
		surface = wl_container_of(cur_lock->surfaces.next, surface, link);
		client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
	} else if (!locked) {
		focusclient(focustop(selmon), 1);
	} else {
		wlr_seat_keyboard_clear_focus(seat);
	}
}

void
configurephys(Monitor *m, const MonitorRule *match)
{
	double width_mm;
	double height_mm;

	if (!m || !m->wlr_output)
		return;

	m->phys = (MonitorPhysical){0};

	width_mm = m->wlr_output->phys_width > 0 ? m->wlr_output->phys_width : 0;
	height_mm = m->wlr_output->phys_height > 0 ? m->wlr_output->phys_height : 0;

	if (width_mm > 0)
		m->phys.width_mm = width_mm;
	if (height_mm > 0)
		m->phys.height_mm = height_mm;

	if (match) {
		if (match->phys.size_is_set) {
			if (match->phys.width_mm > 0) {
				m->phys.width_mm = match->phys.width_mm;
				m->phys.width_configured = 1;
			}
			if (match->phys.height_mm > 0) {
				m->phys.height_mm = match->phys.height_mm;
				m->phys.height_configured = 1;
			}
		}
		if (match->phys.origin_is_set) {
			m->phys.origin_x_mm = match->phys.x_mm;
			m->phys.origin_y_mm = match->phys.y_mm;
			m->phys.origin_configured = 1;
		}
	}
}

void
updatephys(Monitor *m)
{
	double width_mm;
	double height_mm;

	if (!m || !m->wlr_output)
		return;

	width_mm = m->phys.width_mm;
	height_mm = m->phys.height_mm;

	if (!m->phys.width_configured) {
		if (m->wlr_output->phys_width > 0)
			width_mm = m->wlr_output->phys_width;
		if (width_mm <= 0 && m->monitor_area.width > 0)
			width_mm = m->monitor_area.width;
	}
	if (!m->phys.height_configured) {
		if (m->wlr_output->phys_height > 0)
			height_mm = m->wlr_output->phys_height;
		if (height_mm <= 0 && m->monitor_area.height > 0)
			height_mm = m->monitor_area.height;
	}

	m->phys.width_mm = width_mm;
	m->phys.height_mm = height_mm;

	if (m->monitor_area.width > 0 && width_mm > 0)
		m->phys.mm_per_px_x = width_mm / m->monitor_area.width;
	else
		m->phys.mm_per_px_x = 0;
	if (m->monitor_area.height > 0 && height_mm > 0)
		m->phys.mm_per_px_y = height_mm / m->monitor_area.height;
	else
		m->phys.mm_per_px_y = 0;

	if (!m->phys.origin_configured) {
		if (m->phys.mm_per_px_x > 0)
			m->phys.origin_x_mm = m->monitor_area.x * m->phys.mm_per_px_x;
		else
			m->phys.origin_x_mm = 0;
		if (m->phys.mm_per_px_y > 0)
			m->phys.origin_y_mm = m->monitor_area.y * m->phys.mm_per_px_y;
		else
			m->phys.origin_y_mm = 0;
	}
}

/* IPC Protocol Implementation */
static struct zvwl_ipc_manager_v1_interface manager_implementation = {
	.release = ipc_manager_release,
	.get_output = ipc_manager_get_output
};

static struct zvwl_ipc_output_v1_interface output_implementation = {
	.release = ipc_output_release,
	.set_workspace = NULL,
	.set_client_workspace = NULL,
	.set_layout = NULL,
	.set_virtual_output = NULL
};

void
ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *manager_resource = wl_resource_create(client,
		&zvwl_ipc_manager_v1_interface, version, id);

	if (!manager_resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(manager_resource, &manager_implementation, NULL, ipc_manager_destroy);

	/* Send available layouts */
	for (size_t i = 0; i < LENGTH(layouts); i++)
		zvwl_ipc_manager_v1_send_layout(manager_resource, layouts[i].symbol);
}

void
ipc_manager_destroy(struct wl_resource *resource)
{
}

void
ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource,
	uint32_t id, struct wl_resource *output)
{
	IPCOutput *ipc_output;
	Monitor *monitor = wlr_output_from_resource(output)->data;
	struct wl_resource *output_resource = wl_resource_create(client,
		&zvwl_ipc_output_v1_interface, wl_resource_get_version(resource), id);

	if (!output_resource)
		return;

	ipc_output = ecalloc(1, sizeof(*ipc_output));
	ipc_output->resource = output_resource;
	ipc_output->mon = monitor;
	wl_resource_set_implementation(output_resource, &output_implementation, ipc_output, ipc_output_destroy);
	wl_list_insert(&monitor->ipc_outputs, &ipc_output->link);
	ipc_output_printstatus_to(ipc_output);
}

void
ipc_manager_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

void
ipc_output_destroy(struct wl_resource *resource)
{
	IPCOutput *ipc_output = wl_resource_get_user_data(resource);
	wl_list_remove(&ipc_output->link);
	free(ipc_output);
}

void
ipc_output_printstatus(Monitor *monitor)
{
	IPCOutput *ipc_output;
	wl_list_for_each(ipc_output, &monitor->ipc_outputs, link)
		ipc_output_printstatus_to(ipc_output);
}

void
ipc_output_printstatus_to(IPCOutput *ipc_output)
{
	Monitor *monitor = ipc_output->mon;
	Client *c, *focused;
	Workspace *ws;
	VirtualOutput *vout, *active_vout;
	unsigned int count;
	int urgent;
	int is_tabbed = 0, tab_count = 0, tab_index = 0;

	active_vout = focusvout(monitor);
	ws = active_vout ? active_vout->ws : NULL;

	count = 0;
	urgent = 0;
	wl_list_for_each(c, &clients, link) {
		if (c->ws != ws)
			continue;
		count++;
		if (c->isurgent)
			urgent = 1;
	}

	focused = focustop(monitor);

	/* Check if layout is tabbed and count visible clients */
	if (active_vout && active_vout->lt[active_vout->sellt] &&
	    active_vout->lt[active_vout->sellt]->arrange == tabbed) {
		Client *tab;
		int idx = 0;
		is_tabbed = 1;
		wl_list_for_each(tab, &clients, link) {
			if (CLIENT_VOUT(tab) != active_vout || !VISIBLEON(tab, monitor) ||
			    tab->isfloating || client_is_nonvirtual_fullscreen(tab))
				continue;
			if (tab == focused)
				tab_index = idx;
			idx++;
			tab_count++;
		}
	}

	/* Send output status events */
	zvwl_ipc_output_v1_send_active(ipc_output->resource, monitor == selmon);
	zvwl_ipc_output_v1_send_workspace(ipc_output->resource,
		ws ? ws->id : 0, ws ? ws->name : "");
	zvwl_ipc_output_v1_send_title(ipc_output->resource,
		focused ? client_get_title(focused) : "");
	zvwl_ipc_output_v1_send_appid(ipc_output->resource,
		focused ? client_get_appid(focused) : "");
	zvwl_ipc_output_v1_send_fullscreen(ipc_output->resource,
		focused ? focused->isfullscreen : 0);
	zvwl_ipc_output_v1_send_floating(ipc_output->resource,
		focused ? focused->isfloating : 0);
	zvwl_ipc_output_v1_send_tabbed(ipc_output->resource,
		is_tabbed, tab_count, tab_index);

	/* Send individual tab window information if in tabbed mode */
	if (is_tabbed) {
		Client *tab;
		int idx = 0;
		wl_list_for_each(tab, &clients, link) {
			if (CLIENT_VOUT(tab) != active_vout || !VISIBLEON(tab, monitor) ||
			    tab->isfloating || client_is_nonvirtual_fullscreen(tab))
				continue;
			zvwl_ipc_output_v1_send_tab_window(ipc_output->resource,
				idx,
				client_get_title(tab) ? client_get_title(tab) : "",
				client_get_appid(tab) ? client_get_appid(tab) : "",
				tab == focused);
			idx++;
		}
	}

	zvwl_ipc_output_v1_send_urgent(ipc_output->resource, urgent);
	zvwl_ipc_output_v1_send_clients(ipc_output->resource, count);
	zvwl_ipc_output_v1_send_layout_symbol(ipc_output->resource,
		active_vout ? active_vout->ltsymbol : "");

	/* Send virtual output information */
	zvwl_ipc_output_v1_send_virtual_output_begin(ipc_output->resource);

	wl_list_for_each(vout, &monitor->vouts, link) {
		Workspace *ws_iter;
		int ws_idx;
		for (ws_idx = 0; ws_idx < WORKSPACE_COUNT; ws_idx++) {
			const Layout *layout = NULL;
			unsigned int wcount = 0;
			int wurgent = 0;
			ws_iter = &workspaces[ws_idx];
			if (ws_iter->vout != vout)
				continue;
			wl_list_for_each(c, &clients, link) {
				if (c->ws != ws_iter)
					continue;
				wcount++;
				if (c->isurgent)
					wurgent = 1;
			}
			if (!wcount)
				continue;
			if (ws_iter->state.sellt < LENGTH(ws_iter->state.lt))
				layout = ws_iter->state.lt[ws_iter->state.sellt];
			if (!layout)
				layout = vout->lt[vout->sellt];
			zvwl_ipc_output_v1_send_virtual_output(ipc_output->resource,
				vout->id, vout->name, ws_iter == vout->ws,
				ws_iter->id, ws_iter->name,
				wcount, wurgent,
				layout ? layout->symbol : vout->ltsymbol);
		}
	}

	/** emit additional workspaces on the selected monitor (even if
	 * they currently have no vout) */
	if (monitor == selmon) {
		int ws_idx;
		for (ws_idx = 0; ws_idx < WORKSPACE_COUNT; ws_idx++) {
			Workspace *ws_iter = &workspaces[ws_idx];
			VirtualOutput *home = ws_iter->vout;
			Monitor *home_mon = home ? home->mon : NULL;
			const Layout *layout = NULL;
			unsigned int wcount = 0;
			int wurgent = 0;

			if (home_mon && home_mon != monitor)
				continue;

			wl_list_for_each(c, &clients, link) {
				if (c->ws != ws_iter)
					continue;
				wcount++;
				if (c->isurgent)
					wurgent = 1;
			}
			if (!wcount)
				continue;

			if (ws_iter->state.sellt < LENGTH(ws_iter->state.lt))
				layout = ws_iter->state.lt[ws_iter->state.sellt];
			if (!layout && home)
				layout = home->lt[home->sellt];

			zvwl_ipc_output_v1_send_virtual_output(ipc_output->resource,
				home ? home->id : 0,
				home ? home->name : "",
				home && home->ws == ws_iter,
				ws_iter->id, ws_iter->name,
				wcount, wurgent,
				layout ? layout->symbol : (home ? home->ltsymbol : ""));
		}
	}

	zvwl_ipc_output_v1_send_virtual_output_end(ipc_output->resource);
	zvwl_ipc_output_v1_send_frame(ipc_output->resource);
}

void
ipc_output_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

void
updateipc(void)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		ipc_output_printstatus(m);
	update_fullscreen_idle_inhibit();
}

void
update_fullscreen_idle_inhibit(void)
{
	Client *c;
	int requested = 0;

	if (fullscreen_idle_inhibit) {
		wl_list_for_each(c, &clients, link) {
			if (!client_is_unmanaged(c)
					&& c->isfullscreen
					&& VISIBLEON(c, CLIENT_MON(c))) {
				requested = 1;
				break;
			}
		}
	}

	fullscreen_idle_active = requested;
	checkidleinhibitor(NULL);
}

void
checkidleinhibitor(struct wlr_surface *exclude)
{
	int inhibited = fullscreen_idle_active, unused_lx, unused_ly;
	struct wlr_idle_inhibitor_v1 *inhibitor;

	if (!idle_notifier)
		return;

	if (!idle_inhibit_mgr) {
		wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
		return;
	}

	wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
		struct wlr_scene_tree *tree = surface->data;
		if (exclude != surface && (bypass_surface_visibility || (!tree
				|| wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
			inhibited = 1;
			break;
		}
	}

	wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	/* Prepare an XKB keymap and assign it to the keyboard group. */
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules,
				XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");

	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

	/* Set up listeners for keyboard events */
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	/* A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same wlr_keyboard_group, which provides a single wlr_keyboard interface for
	 * all of them. Set this combined wlr_keyboard as the seat keyboard.
	 */
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
chvt(const Arg *arg)
{
	wlr_session_change_vt(session, arg->ui);
}

