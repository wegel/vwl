#ifndef VWL_H
#define VWL_H

#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#endif

/* macros */
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)
#define CLIENT_VOUT(C) ((C)->ws ? (C)->ws->vout : NULL)
#define CLIENT_MON(C) (CLIENT_VOUT(C) ? CLIENT_VOUT(C)->mon : NULL)
#define MON_FOCUS_WS(M) (focusedvout(M) ? focusedvout(M)->ws : NULL)
#define VISIBLEON(C, M) ((M) && (C)->ws && CLIENT_MON(C) == (M) && CLIENT_VOUT(C) && CLIENT_VOUT(C)->ws == (C)->ws)
#define LENGTH(X) (sizeof X / sizeof X[0])
#define END(A) ((A) + LENGTH(A))
#define WORKSPACE_COUNT 256
#define WORKSPACE_NAME_LEN 32
#define LISTEN(E, L, H) wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)                                       \
	do {                                                      \
		struct wl_listener *_l = ecalloc(1, sizeof(*_l)); \
		_l->notify = (H);                                 \
		wl_signal_add((E), _l);                           \
	} while (0)

/* enums */
enum { CurNormal, CurPressed, CurMove, CurResize };				     /* cursor */
enum { XDGShell, LayerShell, X11 };						     /* client types */
enum { LyrBg, LyrBottom, LyrTile, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS }; /* scene layers */
enum { FS_NONE, FS_VIRTUAL, FS_MONITOR };					     /* fullscreen modes */
enum {
	POINTER_REVEAL_EDGE_NONE,
	POINTER_REVEAL_EDGE_TOP,
	POINTER_REVEAL_EDGE_BOTTOM,
	POINTER_REVEAL_EDGE_LEFT,
	POINTER_REVEAL_EDGE_RIGHT,
}; /* pointer reveal edge */
enum TabHdrPos { TABHDR_TOP, TABHDR_BOTTOM };

typedef struct TabTitleTransformRule {
	const char *pattern;
	const char *replacement;
} TabTitleTransformRule;

typedef struct SpawnWorkspaceArg {
	unsigned int workspace_id;
	const char *const *argv;
} SpawnWorkspaceArg;

/* type declarations */
typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct MonitorPhysical MonitorPhysical;
typedef struct CursorPhysical CursorPhysical;
typedef struct HoverState HoverState;
typedef struct Workspace Workspace;
typedef struct VirtualOutput VirtualOutput;
typedef struct Client Client;
typedef struct KeyboardGroup KeyboardGroup;
typedef struct LayerSurface LayerSurface;
typedef struct Layout Layout;
typedef struct WorkspaceState WorkspaceState;
typedef struct MonitorRule MonitorRule;
typedef struct VirtualOutputRule VirtualOutputRule;
typedef struct PointerConstraint PointerConstraint;
typedef struct Rule Rule;
typedef struct SessionLock SessionLock;
typedef struct IPCOutput IPCOutput;
typedef struct IPCManager IPCManager;
typedef struct PendingSpawn PendingSpawn;
struct wlr_ext_image_capture_source_v1;

struct Client {
	/* Must keep this field first */
	unsigned int type; /* XDGShell or X11* */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
	struct wlr_scene_tree *scene_surface;
	struct wlr_scene *image_capture_scene;
	struct wlr_scene_tree *image_capture_tree;
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel;
	struct wlr_ext_image_capture_source_v1 *image_capture_source;
	PendingSpawn *pending_spawn;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom;	       /* layout-relative, includes border */
	struct wlr_box prev;	       /* before entering fullscreen, includes border */
	struct wlr_box bounds;	       /* only width and height are used */
	int float_width, float_height; /* initial or last floating content size, without borders */
	bool float_size_saved;	       /* true after leaving floating mode */
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener image_capture_source_destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
#ifdef XWAYLAND
	struct wl_listener activate;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener configure;
	struct wl_listener set_hints;
	struct wl_listener set_startup_id;
#endif
	unsigned int bw;
	Workspace *ws;
	int isfloating, isurgent, isfullscreen;
	int fullscreen_mode;
	uint32_t resize; /* configure serial of a pending resize */
};

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

struct KeyboardGroup {
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms; /* invalid if nsyms == 0 */
	uint32_t mods;		     /* invalid if nsyms == 0 */
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct LayerSurface {
	/* Must keep this field first */
	unsigned int type; /* LayerShell */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list link;
	int mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
};

struct Layout {
	const char *symbol;
	void (*arrange)(Monitor *);
};

struct WorkspaceState {
	float mfact;
	int nmaster;
	const Layout *lt[2];
	unsigned int sellt;
};

struct Workspace {
	unsigned int id;
	char name[WORKSPACE_NAME_LEN];
	struct wl_list link; /* VirtualOutput.workspaces */
	VirtualOutput *vout;
	WorkspaceState state;
	char orphan_vout_name[WORKSPACE_NAME_LEN];
	char orphan_monitor_name[WORKSPACE_NAME_LEN];
	bool was_orphaned; /* Track if workspace was orphaned during monitor removal */
	bool orphan_was_active;
};

struct VirtualOutput {
	unsigned int id;
	char name[WORKSPACE_NAME_LEN];
	struct wl_list link; /* Monitor.vouts */
	Monitor *mon;
	struct wl_list workspaces;
	Workspace *ws;
	struct wlr_scene_tree *tabhdr;
	struct wlr_ext_image_capture_source_v1 *image_capture_source;
	struct wl_listener image_capture_source_destroy;
	struct wlr_box layout_geom; /* layout-relative geometry */
	struct wlr_box rule_geom;   /* rule geometry in physical pixels */
	float mfact;
	int nmaster;
	const Layout *lt[2];
	unsigned int sellt;
	char ltsymbol[16];
	struct {
		struct wl_signal destroy;
	} events;
};

struct MonitorPhysical {
	double width_mm;
	double height_mm;
	double origin_x_mm;
	double origin_y_mm;
	double mm_per_px_x;
	double mm_per_px_y;
	int width_configured;
	int height_configured;
	int origin_configured;
};

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg; /* See createmon() for info */
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box monitor_area; /* monitor area, layout-relative */
	struct wlr_box window_area;  /* window area, layout-relative */
	struct wl_list layers[4];    /* LayerSurface.link */
	struct wl_list vouts;	     /* VirtualOutput.link */
	VirtualOutput *focus_vout;
	int gamma_lut_changed;
	int asleep;
	MonitorPhysical phys;
};

struct CursorPhysical {
	double x_mm;
	double y_mm;
	Monitor *mon;
	Monitor *last_mon;
};

struct HoverState {
	Monitor *mon;
	VirtualOutput *vout;
	Workspace *ws;
	Client *client;
};

struct MonitorRule {
	const char *name;
	float scale;
	enum wl_output_transform rr;
	int x, y;
	struct {
		double width_mm;
		double height_mm;
		double x_mm;
		double y_mm;
		int size_is_set;
		int origin_is_set;
	} phys;
};

struct VirtualOutputRule {
	const char *monitor;
	const char *name;
	int32_t x, y;
	int32_t width, height;
	float mfact;
	int nmaster;
	const Layout *lt_primary;
	const Layout *lt_secondary;
	const unsigned int *workspaces;
	size_t workspace_count;
};

struct PointerConstraint {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
};

struct Rule {
	const char *id;
	const char *title;
	unsigned int workspace;
	int monitor;
};

struct SessionLock {
	struct wlr_scene_tree *scene;
	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
};

/* Read-only views of config.h for the desktop modules. */
struct WorkspaceConfig {
	const Layout *layouts;
	size_t layout_count;
	const VirtualOutputRule *rules;
	size_t rule_count;
};
struct OutputConfig {
	const MonitorRule *rules;
	size_t rule_count;
	const float *fullscreen_color;
};
struct CursorConfig {
	int sloppy_focus, warp_to_vout, physical_gap_jumps;
};
extern const struct WorkspaceConfig workspace_config;
extern const struct OutputConfig output_config;
extern const struct CursorConfig cursor_config;
extern struct wlr_box sgeom;
extern bool vt_recovery_mode;

/* Shared compositor state. */
extern struct wl_display *dpy;
extern struct wl_event_loop *event_loop;
extern struct wlr_backend *backend;
extern struct wlr_scene *scene;
extern struct wlr_scene_tree *layers[NUM_LAYERS];
extern struct wlr_renderer *drw;
extern struct wlr_allocator *alloc;
extern struct wlr_compositor *compositor;
extern struct wlr_session *session;
extern struct wlr_xdg_shell *xdg_shell;
extern struct wlr_seat *seat;
extern struct wlr_cursor *cursor;
extern struct wlr_xcursor_manager *cursor_mgr;
extern struct wlr_scene_tree *drag_icon;
extern struct wlr_output_layout *output_layout;
extern struct wl_list clients;
extern struct wl_list fstack;
extern struct wl_list mons;
extern Monitor *selmon;
extern Workspace workspaces[WORKSPACE_COUNT];
extern Workspace *selws;
extern VirtualOutput *selvout;
extern HoverState hover;
extern int ipc_pointer_reveal_hover;
extern int ipc_pointer_reveal_edge;
extern KeyboardGroup *kb_group;
extern unsigned int cursor_mode;
extern Client *grabc;
extern int grabcx, grabcy;
extern int locked;
extern void *exclusive_focus;
extern CursorPhysical cursor_phys;
extern pid_t child_pid;
extern struct wlr_idle_notifier_v1 *idle_notifier;
extern struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
extern struct wlr_pointer_constraint_v1 *active_constraint;
extern struct wlr_scene_rect *locked_bg;
extern struct wlr_session_lock_v1 *cur_lock;
#ifdef XWAYLAND
extern struct wlr_xwayland *xwayland;
#endif

#include "client.h"

extern struct wl_listener cursor_axis;
extern struct wl_listener cursor_button;
extern struct wl_listener cursor_frame;
extern struct wl_listener cursor_motion;
extern struct wl_listener cursor_motion_absolute;
extern struct wl_listener gpu_reset;
extern struct wl_listener layout_change;
extern struct wl_listener new_idle_inhibitor;
extern struct wl_listener new_input_device;
extern struct wl_listener new_virtual_keyboard;
extern struct wl_listener new_virtual_pointer;
extern struct wl_listener new_pointer_constraint;
extern struct wl_listener new_output;
extern struct wl_listener new_xdg_toplevel;
extern struct wl_listener new_xdg_popup;
extern struct wl_listener new_xdg_decoration;
extern struct wl_listener new_layer_surface;
extern struct wl_listener output_mgr_apply;
extern struct wl_listener output_mgr_test;
extern struct wl_listener output_power_mgr_set_mode;
extern struct wl_listener request_activate;
extern struct wl_listener request_cursor;
extern struct wl_listener request_set_psel;
extern struct wl_listener request_set_sel;
extern struct wl_listener request_set_cursor_shape;
extern struct wl_listener request_start_drag;
extern struct wl_listener start_drag;
extern struct wl_listener new_session_lock;
#ifdef XWAYLAND
extern struct wl_listener new_xwayland_surface, xwayland_ready;
#endif

/* vwl.c */
void spawn_on_current_workspace(const Arg *arg);
void arrange(Monitor *m);
void createnotify(struct wl_listener *listener, void *data);
void focusclient(Client *c, int lift);
void focusstack(const Arg *arg);
Client *focustop(Monitor *m);
void killclient(const Arg *arg);
void quit(const Arg *arg);
void resize(Client *c, struct wlr_box geo, int interact);
void togglefloating(const Arg *arg);
void setworkspace(Client *c, Workspace *ws);
void spawn(const Arg *arg);
void spawn_on_workspace(const Arg *arg);
void togglefullscreen(const Arg *arg);
void urgent(struct wl_listener *listener, void *data);
void xytonode(double x, double y, struct wlr_surface **psurface, Client **pc, LayerSurface **pl, double *nx,
		double *ny);
void zoom(const Arg *arg);
Client *focustopvout(VirtualOutput *vout);
#ifdef XWAYLAND
void createnotifyx11(struct wl_listener *listener, void *data);
void xwaylandready(struct wl_listener *listener, void *data);
#endif

/* plumbing.c */
void handlesig(int signo);
void cleanup(void);
void requestmonstate(struct wl_listener *listener, void *data);
void setsel(struct wl_listener *listener, void *data);
void rendermon(struct wl_listener *listener, void *data);
void arrangelayers(Monitor *m);
void requestdecorationmode(struct wl_listener *listener, void *data);
void destroylocksurface(struct wl_listener *listener, void *data);
void configurephys(Monitor *m, const MonitorRule *match);
void updatephys(Monitor *m);
void update_fullscreen_idle_inhibit(void);
void checkidleinhibitor(struct wlr_surface *exclude);
KeyboardGroup *createkeyboardgroup(void);
void chvt(const Arg *arg);

/* workspace.c */
void workspaces_init(void);
struct wlr_box voutarea(VirtualOutput *vout);
Workspace *wsbyid(unsigned int id);
VirtualOutput *firstvout(Monitor *m);
VirtualOutput *voutbyid(unsigned int id);
VirtualOutput *findvoutbyname(Monitor *m, const char *name);
int ipc_set_workspace_by_id(unsigned int workspace_id);
int ipc_focus_virtual_output(VirtualOutput *vout);
int ipc_move_workspace_to_vout(Workspace *ws, VirtualOutput *vout);
VirtualOutput *focusedvout(Monitor *m);
VirtualOutput *voutat(Monitor *m, double lx, double ly);
void arrangevout(Monitor *m, const struct wlr_box *usable_area);
VirtualOutput *createvout(Monitor *m, const char *name);
void destroyvout(VirtualOutput *vout);
Workspace *wsfindfree(void);
void attachvoutworkspaces(VirtualOutput *vout, const unsigned int *workspace_ids, size_t workspace_count);
void wssave(VirtualOutput *vout);
Workspace *wsfirst(VirtualOutput *vout);
void wsattach(VirtualOutput *vout, Workspace *ws);
void wsactivate(VirtualOutput *vout, Workspace *ws, int focus_change);
void wsmoveto(Workspace *ws, VirtualOutput *vout);
void view(const Arg *arg);
void moveworkspace(const Arg *arg);
void tag(const Arg *arg);
void tagmon(const Arg *arg);
void focusvout(const Arg *arg);

/* cursor.c */
void cursor_init(void);
void cursorwarptovout(VirtualOutput *vout);
void motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy, double dx_unaccel,
		double dy_unaccel);
void moveresize(const Arg *arg);
void cursorwarptohint(void);
void cursorsync(void);

/* output.c */
void focusmon(const Arg *arg);
void output_init(struct wlr_scene_rect *background);
void createmon(struct wl_listener *listener, void *data);
void updatemons(struct wl_listener *listener, void *data);
Monitor *monitorbyname(const char *name);
Monitor *dirtomon(enum wlr_direction dir);
Monitor *dirtomonfrom(Monitor *from, enum wlr_direction dir);
Monitor *xytomon(double x, double y);

/* debug.c */
void debugstate(const Arg *arg);

/* layout.c */
void tabbed(Monitor *m);
void tile(Monitor *m);
void tabmove(const Arg *arg);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void toggletabbed(const Arg *arg);
Client *focustoptiledvout(VirtualOutput *vout);

/* physical.c */
void updatephysorigins(void);

#endif
