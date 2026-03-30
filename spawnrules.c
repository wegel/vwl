#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vwl.h"
#include "spawnrules.h"

typedef struct PendingSpawn PendingSpawn;

struct PendingSpawn {
	struct wl_list link;
	pid_t pid;
	unsigned int workspace_id;
	char monitor_name[128];
	char vout_name[WORKSPACE_NAME_LEN];
	struct timespec created_at;
};

static struct wl_list pending_spawns;

static void prune_pending_spawns(void);
static int pid_descends_from(pid_t pid, pid_t ancestor);
static VirtualOutput *pending_spawn_target_vout(PendingSpawn *pending);
static PendingSpawn *claim_pending_spawn(Client *c);
static pid_t spawn_command(const char *command);

void
spawnrules_init(void)
{
	wl_list_init(&pending_spawns);
}

void
spawnrules_finish(void)
{
	PendingSpawn *pending, *tmp;

	wl_list_for_each_safe(pending, tmp, &pending_spawns, link) {
		wl_list_remove(&pending->link);
		free(pending);
	}
}

static void
prune_pending_spawns(void)
{
	PendingSpawn *pending, *tmp;
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return;

	wl_list_for_each_safe(pending, tmp, &pending_spawns, link) {
		if (now.tv_sec - pending->created_at.tv_sec > 30) {
			wl_list_remove(&pending->link);
			free(pending);
		}
	}
}

static int
pid_descends_from(pid_t pid, pid_t ancestor)
{
	char path[64];
	char line[256];
	FILE *fp;
	pid_t current = pid;

	if (pid <= 0 || ancestor <= 0)
		return 0;
	if (pid == ancestor)
		return 1;

	while (current > 1) {
		pid_t parent = -1;

		snprintf(path, sizeof(path), "/proc/%d/status", current);
		fp = fopen(path, "r");
		if (!fp)
			return 0;
		while (fgets(line, sizeof(line), fp)) {
			if (!strncmp(line, "PPid:", 5)) {
				parent = (pid_t)strtol(line + 5, NULL, 10);
				break;
			}
		}
		fclose(fp);

		if (parent <= 0)
			return 0;
		if (parent == ancestor)
			return 1;
		if (parent == current)
			return 0;
		current = parent;
	}

	return 0;
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
claim_pending_spawn(Client *c)
{
	PendingSpawn *pending, *tmp;
	pid_t pid;

	prune_pending_spawns();
	pid = client_get_pid(c);
	if (pid <= 0)
		return NULL;

	wl_list_for_each_safe(pending, tmp, &pending_spawns, link) {
		if (!pid_descends_from(pid, pending->pid))
			continue;
		wl_list_remove(&pending->link);
		return pending;
	}

	return NULL;
}

bool
spawnrules_apply(Client *c)
{
	PendingSpawn *pending;
	Workspace *target_ws;
	VirtualOutput *target_vout;

	pending = claim_pending_spawn(c);
	if (!pending)
		return false;

	target_ws = wsbyid(pending->workspace_id);
	target_vout = pending_spawn_target_vout(pending);

	if (target_ws && target_vout && !target_ws->vout)
		wsattach(target_vout, target_ws);
	if (target_ws)
		setworkspace(c, target_ws);
	free(pending);
	return true;
}

static pid_t
spawn_command(const char *command)
{
	pid_t pid;

	if (!command || !command[0])
		return -1;
	if ((pid = fork()) == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", command, NULL);
		die("vwl: execl /bin/sh -c failed:");
	}
	return pid;
}

int
ipc_spawn_on_workspace(unsigned int workspace_id, const char *command)
{
	PendingSpawn *pending;
	VirtualOutput *vout;
	pid_t pid;

	if (!wsbyid(workspace_id) || !command || !command[0])
		return -1;

	/* For this spike, the IPC takes a single shell command string instead of
	 * an argv array to keep the wire format and client-side encoding simple. */
	pid = spawn_command(command);
	if (pid <= 0)
		return -1;

	pending = ecalloc(1, sizeof(*pending));
	pending->pid = pid;
	pending->workspace_id = workspace_id;
	if (clock_gettime(CLOCK_MONOTONIC, &pending->created_at) < 0) {
		pending->created_at.tv_sec = 0;
		pending->created_at.tv_nsec = 0;
	}
	if (selmon && selmon->wlr_output)
		snprintf(pending->monitor_name, sizeof(pending->monitor_name), "%s", selmon->wlr_output->name);
	vout = selmon ? focusedvout(selmon) : NULL;
	if (vout)
		snprintf(pending->vout_name, sizeof(pending->vout_name), "%s", vout->name);
	wl_list_insert(&pending_spawns, &pending->link);
	return 0;
}
