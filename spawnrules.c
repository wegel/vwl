#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/types/wlr_xdg_activation_v1.h>

#include "vwl.h"
#include "spawnrules.h"

struct PendingSpawn {
	struct wl_list link;
	pid_t pid;
	unsigned int workspace_id;
	char monitor_name[128];
	char vout_name[WORKSPACE_NAME_LEN];
	uint32_t x11_group; /* Window carrying the startup ID, or its WM_HINTS group */
	struct wlr_xdg_activation_token_v1 *token;
	struct wl_listener token_destroy;
};

static struct wl_list pending_spawns;
static struct wlr_xdg_activation_v1 *xdg_activation;

static void destroypendingspawn(PendingSpawn *pending);
static PendingSpawn *creatependingspawn(unsigned int workspace_id);
static PendingSpawn *claimpendingspawn(PendingSpawn *pending);
static VirtualOutput *pending_spawn_target_vout(PendingSpawn *pending);
static PendingSpawn *findpendingspawnforclient(Client *c, bool *keep_pending);
static pid_t spawn_argv(const char *const argv[], const char *token);
static pid_t spawn_command(const char *command, const char *token);

void
spawnrules_init(struct wlr_xdg_activation_v1 *activation)
{
	wl_list_init(&pending_spawns);
	xdg_activation = activation;
}

void
spawnrules_finish(void)
{
	PendingSpawn *pending, *tmp;

	wl_list_for_each_safe(pending, tmp, &pending_spawns, link) destroypendingspawn(pending);
	xdg_activation = NULL;
}

static void
destroypendingspawn(PendingSpawn *pending)
{
	struct wlr_xdg_activation_token_v1 *token;

	if (!pending)
		return;
	wl_list_remove(&pending->link);
	if ((token = pending->token)) {
		pending->token = NULL;
		token->data = NULL;
		wl_list_remove(&pending->token_destroy.link);
		wlr_xdg_activation_token_v1_destroy(token);
	}
	free(pending);
}

static void
pendingspawntokendestroy(struct wl_listener *listener, void *data)
{
	PendingSpawn *pending = wl_container_of(listener, pending, token_destroy);
	(void)data;

	pending->token->data = NULL;
	pending->token = NULL;
	wl_list_remove(&pending->token_destroy.link);
	destroypendingspawn(pending);
}

static PendingSpawn *
creatependingspawn(unsigned int workspace_id)
{
	PendingSpawn *pending;
	VirtualOutput *vout;

	if (!xdg_activation)
		return NULL;
	pending = ecalloc(1, sizeof(*pending));
	wl_list_init(&pending->link);
	pending->workspace_id = workspace_id;
	if (selmon && selmon->wlr_output)
		snprintf(pending->monitor_name, sizeof(pending->monitor_name), "%s", selmon->wlr_output->name);
	vout = selmon ? focusedvout(selmon) : NULL;
	if (vout)
		snprintf(pending->vout_name, sizeof(pending->vout_name), "%s", vout->name);

	pending->token = wlr_xdg_activation_token_v1_create(xdg_activation);
	if (!pending->token) {
		free(pending);
		return NULL;
	}
	pending->token->data = pending;
	LISTEN(&pending->token->events.destroy, &pending->token_destroy, pendingspawntokendestroy);
	wl_list_insert(&pending_spawns, &pending->link);
	return pending;
}

static PendingSpawn *
claimpendingspawn(PendingSpawn *pending)
{
	struct wlr_xdg_activation_token_v1 *token;

	if (!pending)
		return NULL;
	wl_list_remove(&pending->link);
	wl_list_init(&pending->link);
	if ((token = pending->token)) {
		pending->token = NULL;
		token->data = NULL;
		wl_list_remove(&pending->token_destroy.link);
		wl_list_init(&pending->token_destroy.link);
	}
	return pending;
}

static VirtualOutput *
pending_spawn_target_vout(PendingSpawn *pending)
{
	Monitor *mon;
	VirtualOutput *vout;

	if (!pending)
		return NULL;
	if (pending->monitor_name[0]) {
		mon = monitorbyname(pending->monitor_name);
		if (mon) {
			if (pending->vout_name[0]) {
				vout = findvoutbyname(mon, pending->vout_name);
				if (vout)
					return vout;
			}
			return focusedvout(mon);
		}
	}
	return selmon ? focusedvout(selmon) : NULL;
}

static PendingSpawn *
findpendingspawnforclient(Client *c, bool *keep_pending)
{
	PendingSpawn *pending;
	pid_t pid;
	uint32_t x11_group = 0, x11_window = 0;

	*keep_pending = false;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		x11_window = c->surface.xwayland->window_id;
		if (c->surface.xwayland->hints)
			x11_group = c->surface.xwayland->hints->window_group;
	}
#endif

	pid = client_get_pid(c);
	if (pid <= 0)
		return NULL;

	wl_list_for_each(pending, &pending_spawns, link) {
		if (pending->x11_group) {
			/* GTK can put the startup ID on an invisible X11 group leader. Visible
			 * windows point back to it through WM_HINTS.window_group. */
			if (pending->x11_group == x11_window || pending->x11_group == x11_group) {
				*keep_pending = true;
				return pending;
			}
			continue;
		}
		/* spawn_argv() calls setsid(), so each process in this launch reports
		 * pending->pid as its session ID. Keep the record until wlroots expires
		 * its token so splash and main windows both match. */
		if (getsid(pid) != pending->pid)
			continue;
		*keep_pending = true;
		return pending;
	}

	return NULL;
}

bool
spawnrules_bind_token(Client *c, struct wlr_xdg_activation_token_v1 *token)
{
	PendingSpawn *pending;

	if (!c || !token || !(pending = token->data) || pending->token != token)
		return false;
	spawnrules_forget(c);
	c->pending_spawn = claimpendingspawn(pending);
	return true;
}

bool
spawnrules_track_startup_id(uint32_t x11_window, uint32_t x11_group, const char *startup_id)
{
	PendingSpawn *pending;
	struct wlr_xdg_activation_token_v1 *token;

	if (!x11_window || !xdg_activation || !startup_id)
		return false;
	token = wlr_xdg_activation_v1_find_token(xdg_activation, startup_id);
	if (!token || !(pending = token->data) || pending->token != token)
		return false;
	pending->x11_group = x11_group ? x11_group : x11_window;
	return true;
}

void
spawnrules_forget(Client *c)
{
	PendingSpawn *pending;

	if (!c || !(pending = c->pending_spawn))
		return;
	c->pending_spawn = NULL;
	destroypendingspawn(pending);
}

bool
spawnrules_apply(Client *c)
{
	PendingSpawn *pending;
	Workspace *target_ws;
	VirtualOutput *target_vout;
	bool keep_pending = false;

	if (!c)
		return false;
	pending = c->pending_spawn;
	if (pending)
		c->pending_spawn = NULL;
	else
		pending = findpendingspawnforclient(c, &keep_pending);
	if (!pending)
		return false;

	target_ws = wsbyid(pending->workspace_id);
	target_vout = pending_spawn_target_vout(pending);

	if (target_ws && target_vout && !target_ws->vout)
		wsattach(target_vout, target_ws);
	if (target_ws)
		setworkspace(c, target_ws);
	if (!keep_pending)
		destroypendingspawn(pending);
	return true;
}

static void
setactivationenv(const char *token)
{
	if (!token)
		return;
	if (setenv("XDG_ACTIVATION_TOKEN", token, 1) < 0 || setenv("DESKTOP_STARTUP_ID", token, 1) < 0)
		die("vwl: failed to export activation token:");
}

static pid_t
spawn_argv(const char *const argv[], const char *token)
{
	pid_t pid;

	if (!argv || !argv[0])
		return -1;
	if ((pid = fork()) == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		setactivationenv(token);
		execvp(((char **)argv)[0], (char **)argv);
		die("vwl: execvp %s failed:", ((char **)argv)[0]);
	}
	return pid;
}

static pid_t
spawn_command(const char *command, const char *token)
{
	pid_t pid;

	if (!command || !command[0])
		return -1;
	if ((pid = fork()) == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		setactivationenv(token);
		execl("/bin/sh", "/bin/sh", "-c", command, NULL);
		die("vwl: execl /bin/sh -c failed:");
	}
	return pid;
}

int
spawnrules_spawn_on_workspace_argv(unsigned int workspace_id, const char *const argv[])
{
	PendingSpawn *pending;
	pid_t pid;

	if (!wsbyid(workspace_id) || !argv || !argv[0])
		return -1;
	if (!(pending = creatependingspawn(workspace_id)))
		return -1;
	pid = spawn_argv(argv, wlr_xdg_activation_token_v1_get_name(pending->token));
	if (pid <= 0) {
		destroypendingspawn(pending);
		return -1;
	}
	pending->pid = pid;
	return 0;
}

int
ipc_spawn_on_workspace(unsigned int workspace_id, const char *command)
{
	PendingSpawn *pending;
	pid_t pid;

	if (!wsbyid(workspace_id) || !command || !command[0])
		return -1;
	/* For this spike, the IPC takes a single shell command string instead of
	 * an argv array to keep the wire format and client-side encoding simple. */
	if (!(pending = creatependingspawn(workspace_id)))
		return -1;
	pid = spawn_command(command, wlr_xdg_activation_token_v1_get_name(pending->token));
	if (pid <= 0) {
		destroypendingspawn(pending);
		return -1;
	}
	pending->pid = pid;
	return 0;
}
