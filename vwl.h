#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
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
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define CLIENT_VOUT(C)          ((C)->ws ? (C)->ws->vout : NULL)
#define CLIENT_MON(C)           (CLIENT_VOUT(C) ? CLIENT_VOUT(C)->mon : NULL)
#define MON_FOCUS_WS(M)         (focusvout(M) ? focusvout(M)->ws : NULL)
#define VISIBLEON(C, M)         ((M) && (C)->ws && CLIENT_MON(C) == (M) \
                                && CLIENT_VOUT(C) && CLIENT_VOUT(C)->ws == (C)->ws)
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define WORKSPACE_COUNT         256
#define WORKSPACE_NAME_LEN      32
#define DEFAULT_WORKSPACE_ID    1
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof(*_l)); _l->notify = (H); wl_signal_add((E), _l); } while (0)

/* enums */
enum { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */
enum { XDGShell, LayerShell, X11 }; /* client types */
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS }; /* scene layers */
enum { FS_NONE, FS_VIRTUAL, FS_MONITOR }; /* fullscreen modes */
enum TabHdrPos { TABHDR_TOP, TABHDR_BOTTOM };

typedef struct {
	const char *pattern;
	const char *replacement;
} TabTitleTransformRule;

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

struct Client {
	/* Must keep this field first */
	unsigned int type; /* XDGShell or X11* */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom; /* layout-relative, includes border */
	struct wlr_box prev; /* layout-relative, includes border */
	struct wlr_box bounds; /* only width and height are used */
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
	uint32_t mods; /* invalid if nsyms == 0 */
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
};

struct VirtualOutput {
	unsigned int id;
	char name[WORKSPACE_NAME_LEN];
	struct wl_list link; /* Monitor.vouts */
	Monitor *mon;
	struct wl_list workspaces;
	Workspace *ws;
	struct wlr_scene_tree *tabhdr;
	struct wlr_box layout_geom;       /* layout-relative geometry */
	struct wlr_box rule_geom;         /* rule geometry in physical pixels */
	float mfact;
	int nmaster;
	const Layout *lt[2];
	unsigned int sellt;
	char ltsymbol[16];
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
	struct wl_list layers[4]; /* LayerSurface.link */
	struct wl_list vouts; /* VirtualOutput.link */
	struct wl_list ipc_outputs; /* IPCOutput.link */
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
};

struct PointerConstraint {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
};

struct Rule {
	const char *id;
	const char *title;
	unsigned int workspace;
	int isfloating;
	int monitor;
};

struct SessionLock {
	struct wlr_scene_tree *scene;
	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
};

struct IPCOutput {
	struct wl_list link;
	struct wl_resource *resource;
	Monitor *mon;
};

struct IPCManager {
	struct wl_resource *resource;
};

/* Global variables - declarations (defined in plumbing.c) */
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

/* Helper functions from util.h */
void *ecalloc(size_t nmemb, size_t size);
void die(const char *fmt, ...);

/* Functions in plumbing.c */
void handlesig(int signo);
void cleanup(void);
void cleanuplisteners(void);
void destroykeyboardgroup(struct wl_listener *listener, void *data);
void axisnotify(struct wl_listener *listener, void *data);
void buttonpress(struct wl_listener *listener, void *data);
void cursorframe(struct wl_listener *listener, void *data);
void motionrelative(struct wl_listener *listener, void *data);
void motionabsolute(struct wl_listener *listener, void *data);
void gpureset(struct wl_listener *listener, void *data);
void inputdevice(struct wl_listener *listener, void *data);
void createidleinhibitor(struct wl_listener *listener, void *data);
void update_fullscreen_idle_inhibit(void);
void virtualkeyboard(struct wl_listener *listener, void *data);
void virtualpointer(struct wl_listener *listener, void *data);
void requeststartdrag(struct wl_listener *listener, void *data);
void requestmonstate(struct wl_listener *listener, void *data);
void setcursor(struct wl_listener *listener, void *data);
void setcursorshape(struct wl_listener *listener, void *data);
void setsel(struct wl_listener *listener, void *data);
void setpsel(struct wl_listener *listener, void *data);
void startdrag(struct wl_listener *listener, void *data);
void createpointerconstraint(struct wl_listener *listener, void *data);
void createpopup(struct wl_listener *listener, void *data);
int keybinding(uint32_t mods, xkb_keysym_t sym);
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);
void outputmgrapply(struct wl_listener *listener, void *data);
void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
void outputmgrtest(struct wl_listener *listener, void *data);
void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy, uint32_t time);
void rendermon(struct wl_listener *listener, void *data);
void createlayersurface(struct wl_listener *listener, void *data);
void createlocksurface(struct wl_listener *listener, void *data);
void powermgrsetmode(struct wl_listener *listener, void *data);
void createdecoration(struct wl_listener *listener, void *data);
void locksession(struct wl_listener *listener, void *data);
void commitpopup(struct wl_listener *listener, void *data);
void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
void cursorwarptohint(void);
void cursorinteg(double dx, double dy);
int cursorgap(Monitor *origin, double prev_mm_x, double prev_mm_y);
void destroylock(SessionLock *lock, int unlock);
void arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive);

/* Functions needed by plumbing.c from vwl.c */
void cursorsync(void);
Monitor *xytomon(double x, double y);
void motionnotify(uint32_t time, struct wlr_input_device *device, double dx,
		double dy, double dx_unaccel, double dy_unaccel);
void focusclient(Client *c, int lift);
Client *focustop(Monitor *m);
void arrangelayers(Monitor *m);
void arrangevout(Monitor *m, const struct wlr_box *usable_area);
void arrange(Monitor *m);
void commitlayersurfacenotify(struct wl_listener *listener, void *data);
void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
void destroylayersurfacenotify(struct wl_listener *listener, void *data);
void destroylocksurface(struct wl_listener *listener, void *data);
void requestdecorationmode(struct wl_listener *listener, void *data);
void destroydecoration(struct wl_listener *listener, void *data);
void destroysessionlock(struct wl_listener *listener, void *data);
void unlocksession(struct wl_listener *listener, void *data);
void quit(const Arg *arg);
VirtualOutput *focusvout(Monitor *m);
void ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
void ipc_manager_destroy(struct wl_resource *resource);
void ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *output);
void ipc_manager_release(struct wl_client *client, struct wl_resource *resource);
void ipc_output_destroy(struct wl_resource *resource);
void ipc_output_printstatus(Monitor *monitor);
void ipc_output_printstatus_to(IPCOutput *ipc_output);
void ipc_output_release(struct wl_client *client, struct wl_resource *resource);
void updateipc(void);
void configurephys(Monitor *m, const MonitorRule *match);
void updatephys(Monitor *m);

/* From client.h - might need to include this properly later */
#include "client.h"
