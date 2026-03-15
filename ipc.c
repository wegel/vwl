#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_output_management_v1.h>

#include "vwl.h"
#include "ipc.h"
#include "util.h"

#define IPC_CLIENT_BUFFER 4096

typedef struct IPCClient {
	struct wl_list link;
	int fd;
	int subscribed;
	size_t used;
	char buffer[IPC_CLIENT_BUFFER];
	struct wl_event_source *source;
} IPCClient;

static struct {
	int listen_fd;
	char path[PATH_MAX];
	struct wl_event_source *listen_source;
	struct wl_list clients;
} ipc_server = {
	.listen_fd = -1,
};

static void ipc_client_destroy(IPCClient *client);
static int ipc_listen_ready(int fd, uint32_t mask, void *data);
static int ipc_client_ready(int fd, uint32_t mask, void *data);
static int ipc_send_line(IPCClient *client, const char *line);
static int handle_request(IPCClient *client, const char *line);
static int handle_client_buffer(IPCClient *client);
void tabbed(Monitor *m);

static const char *
skip_ws(const char *p)
{
	while (p && *p && isspace((unsigned char)*p))
		p++;
	return p;
}

static const char *
find_key(const char *json, const char *key)
{
	char needle[64];
	const char *p;

	snprintf(needle, sizeof(needle), "\"%s\"", key);
	p = strstr(json, needle);
	if (!p)
		return NULL;
	p += strlen(needle);
	p = skip_ws(p);
	if (!p || *p != ':')
		return NULL;
	return skip_ws(p + 1);
}

static int
json_get_int(const char *json, const char *key, int *value)
{
	char *end;
	long parsed;
	const char *p = find_key(json, key);

	if (!p)
		return 0;

	errno = 0;
	parsed = strtol(p, &end, 10);
	if (errno || end == p)
		return -1;

	*value = (int)parsed;
	return 1;
}

static int
json_get_string(const char *json, const char *key, char *out, size_t out_sz)
{
	size_t i = 0;
	const char *p = find_key(json, key);

	if (!p)
		return 0;
	if (*p != '"')
		return -1;
	p++;

	while (*p && *p != '"') {
		char ch = *p++;
		if (ch == '\\') {
			if (!*p)
				return -1;
			ch = *p++;
			switch (ch) {
			case '"':
			case '\\':
			case '/':
				break;
			case 'b':
				ch = '\b';
				break;
			case 'f':
				ch = '\f';
				break;
			case 'n':
				ch = '\n';
				break;
			case 'r':
				ch = '\r';
				break;
			case 't':
				ch = '\t';
				break;
			default:
				return -1;
			}
		}
		if (i + 1 >= out_sz)
			return -1;
		out[i++] = ch;
	}

	if (*p != '"')
		return -1;
	out[i] = '\0';
	return 1;
}

static void
json_write_escaped(FILE *fp, const char *value)
{
	const unsigned char *p = (const unsigned char *)(value ? value : "");

	fputc('"', fp);
	for (; *p; p++) {
		switch (*p) {
		case '"':
			fputs("\\\"", fp);
			break;
		case '\\':
			fputs("\\\\", fp);
			break;
		case '\b':
			fputs("\\b", fp);
			break;
		case '\f':
			fputs("\\f", fp);
			break;
		case '\n':
			fputs("\\n", fp);
			break;
		case '\r':
			fputs("\\r", fp);
			break;
		case '\t':
			fputs("\\t", fp);
			break;
		default:
			if (*p < 0x20)
				fprintf(fp, "\\u%04x", *p);
			else
				fputc(*p, fp);
			break;
		}
	}
	fputc('"', fp);
}

static void
json_write_box(FILE *fp, const struct wlr_box *box)
{
	fprintf(fp, "{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
		box->x, box->y, box->width, box->height);
}

static void
workspace_stats(Workspace *ws, unsigned int *count, int *urgent)
{
	Client *c;

	*count = 0;
	*urgent = 0;
	wl_list_for_each(c, &clients, link) {
		if (c->ws != ws)
			continue;
		(*count)++;
		if (c->isurgent)
			*urgent = 1;
	}
}

static char *
build_snapshot(void)
{
	char *buf = NULL;
	size_t size = 0;
	FILE *fp = open_memstream(&buf, &size);
	Monitor *m;
	VirtualOutput *vout;
	int i;

	if (!fp)
		return NULL;

	fprintf(fp, "{\"type\":\"snapshot\",");

	fprintf(fp, "\"focused_output\":");
	if (selmon && selmon->wlr_output)
		json_write_escaped(fp, selmon->wlr_output->name);
	else
		fputs("null", fp);

	fprintf(fp, ",\"focused_virtual_output\":");
	if (selvout)
		fprintf(fp, "%u", selvout->id);
	else
		fputs("null", fp);

	fprintf(fp, ",\"focused_workspace\":");
	if (selws)
		fprintf(fp, "%u", selws->id);
	else
		fputs("null", fp);

	fputs(",\"outputs\":[", fp);
	{
		bool first = true;
		wl_list_for_each(m, &mons, link) {
			Client *focused;
			VirtualOutput *active_vout;

			if (!first)
				fputc(',', fp);
			first = false;

			fprintf(fp, "{\"name\":");
			json_write_escaped(fp, m->wlr_output ? m->wlr_output->name : "");
			fprintf(fp, ",\"focused\":%s", m == selmon ? "true" : "false");
			fputs(",\"geometry\":", fp);
			json_write_box(fp, &m->monitor_area);
			fputs(",\"workarea\":", fp);
			json_write_box(fp, &m->window_area);

			active_vout = focusvout(m);
			fprintf(fp, ",\"active_virtual_output\":");
			if (active_vout)
				fprintf(fp, "%u", active_vout->id);
			else
				fputs("null", fp);

			focused = focustop(m);
			fputs(",\"active_window\":", fp);
			if (!focused) {
				fputs("null", fp);
			} else {
				fprintf(fp, "{\"title\":");
				json_write_escaped(fp, client_get_title(focused));
				fprintf(fp, ",\"appid\":");
				json_write_escaped(fp, client_get_appid(focused));
				fprintf(fp, ",\"fullscreen\":%s", focused->isfullscreen ? "true" : "false");
				fprintf(fp, ",\"floating\":false");
				fprintf(fp, ",\"tabbed\":%s}", active_vout && active_vout->lt[active_vout->sellt]
					&& active_vout->lt[active_vout->sellt]->arrange == tabbed ? "true" : "false");
			}

			fputc('}', fp);
		}
	}
	fputc(']', fp);

	fputs(",\"virtual_outputs\":[", fp);
	{
		bool first = true;
		wl_list_for_each(m, &mons, link) {
			wl_list_for_each(vout, &m->vouts, link) {
				unsigned int count = 0;
				int urgent = 0;

				if (!first)
					fputc(',', fp);
				first = false;

				if (vout->ws)
					workspace_stats(vout->ws, &count, &urgent);

				fprintf(fp, "{\"id\":%u,\"name\":", vout->id);
				json_write_escaped(fp, vout->name);
				fprintf(fp, ",\"focused\":%s", vout == selvout ? "true" : "false");
				fprintf(fp, ",\"workspace\":");
				if (vout->ws)
					fprintf(fp, "%u", vout->ws->id);
				else
					fputs("null", fp);
				fprintf(fp, ",\"workspace_name\":");
				if (vout->ws)
					json_write_escaped(fp, vout->ws->name);
				else
					fputs("null", fp);
				fprintf(fp, ",\"layout\":");
				json_write_escaped(fp, vout->ltsymbol);
				fprintf(fp, ",\"clients\":%u,\"urgent\":%s", count, urgent ? "true" : "false");
				fprintf(fp, ",\"outputs\":[");
				json_write_escaped(fp, m->wlr_output ? m->wlr_output->name : "");
				fprintf(fp, "],\"regions\":[{\"output\":");
				json_write_escaped(fp, m->wlr_output ? m->wlr_output->name : "");
				fprintf(fp, ",\"geometry\":");
				json_write_box(fp, &vout->layout_geom);
				fprintf(fp, "}]}");
			}
		}
	}
	fputc(']', fp);

	fputs(",\"workspaces\":[", fp);
	{
		bool first = true;
		for (i = 0; i < WORKSPACE_COUNT; i++) {
			Workspace *ws = &workspaces[i];
			unsigned int count;
			int urgent;

			workspace_stats(ws, &count, &urgent);
			if (!ws->vout && ws != selws && !urgent && count == 0)
				continue;

			if (!first)
				fputc(',', fp);
			first = false;

			fprintf(fp, "{\"id\":%u,\"name\":", ws->id);
			json_write_escaped(fp, ws->name);
			fprintf(fp, ",\"assigned\":%s", ws->vout ? "true" : "false");
			fprintf(fp, ",\"visible\":%s", ws->vout && ws->vout->ws == ws ? "true" : "false");
			fprintf(fp, ",\"focused\":%s", ws == selws ? "true" : "false");
			fprintf(fp, ",\"clients\":%u,\"urgent\":%s", count, urgent ? "true" : "false");
			fprintf(fp, ",\"output\":");
			if (ws->vout && ws->vout->mon && ws->vout->mon->wlr_output)
				json_write_escaped(fp, ws->vout->mon->wlr_output->name);
			else
				fputs("null", fp);
			fprintf(fp, ",\"virtual_output\":");
			if (ws->vout)
				fprintf(fp, "%u", ws->vout->id);
			else
				fputs("null", fp);
			fprintf(fp, ",\"virtual_output_name\":");
			if (ws->vout)
				json_write_escaped(fp, ws->vout->name);
			else
				fputs("null", fp);
			fputc('}', fp);
		}
	}
	fputs("]}", fp);

	fclose(fp);
	return buf;
}

static char *
build_ok_reply(int id)
{
	char *buf = NULL;
	size_t size = 0;
	FILE *fp = open_memstream(&buf, &size);

	if (!fp)
		return NULL;

	fprintf(fp, "{\"id\":%d,\"ok\":true}", id);
	fclose(fp);
	return buf;
}

static char *
build_error_reply(int id, const char *error)
{
	char *buf = NULL;
	size_t size = 0;
	FILE *fp = open_memstream(&buf, &size);

	if (!fp)
		return NULL;

	fprintf(fp, "{\"id\":%d,\"ok\":false,\"error\":", id);
	json_write_escaped(fp, error);
	fputc('}', fp);
	fclose(fp);
	return buf;
}

static char *
build_state_reply(int id, const char *snapshot)
{
	char *buf = NULL;
	size_t size = 0;
	FILE *fp = open_memstream(&buf, &size);

	if (!fp)
		return NULL;

	fprintf(fp, "{\"id\":%d,\"ok\":true,\"state\":", id);
	fputs(snapshot, fp);
	fputc('}', fp);
	fclose(fp);
	return buf;
}

static char *
build_event(const char *snapshot)
{
	char *buf = NULL;
	size_t size = 0;
	FILE *fp = open_memstream(&buf, &size);

	if (!fp)
		return NULL;

	fprintf(fp, "{\"type\":\"event\",\"event\":\"state\",\"state\":");
	fputs(snapshot, fp);
	fputc('}', fp);
	fclose(fp);
	return buf;
}

static int
ipc_send_line(IPCClient *client, const char *line)
{
	size_t len = strlen(line);
	size_t off = 0;

	while (off < len) {
		ssize_t n = send(client->fd, line + off, len - off, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		off += (size_t)n;
	}

	if (send(client->fd, "\n", 1, MSG_NOSIGNAL) != 1)
		return -1;
	return 0;
}

static int
ipc_send_or_drop(IPCClient *client, char *line)
{
	if (!line || ipc_send_line(client, line) < 0) {
		ipc_client_destroy(client);
		free(line);
		return -1;
	}
	free(line);
	return 0;
}

static VirtualOutput *
resolve_vout(const char *json, const char **error)
{
	int vout_id;
	char output_name[128];
	char vout_name[WORKSPACE_NAME_LEN];
	int got_output, got_name;
	Monitor *m;
	VirtualOutput *vout;

	got_output = json_get_string(json, "output", output_name, sizeof(output_name));
	got_name = json_get_string(json, "vout_name", vout_name, sizeof(vout_name));
	if (json_get_int(json, "vout_id", &vout_id) > 0) {
		vout = voutbyid((unsigned int)vout_id);
		if (!vout)
			*error = "unknown virtual output";
		return vout;
	}

	if (got_name <= 0) {
		*error = "missing virtual output reference";
		return NULL;
	}

	if (got_output <= 0) {
		*error = "missing output for named virtual output";
		return NULL;
	}

	m = monitorbyname(output_name);
	if (!m) {
		*error = "unknown output";
		return NULL;
	}

	if (!(vout = findvoutbyname(m, vout_name))) {
		*error = "unknown virtual output";
		return NULL;
	}

	return vout;
}

static int
handle_request(IPCClient *client, const char *line)
{
	char type[64];
	int id = 0;
	int workspace_id;
	int got_type;
	const char *error = NULL;
	char *reply = NULL;

	if (json_get_int(line, "id", &id) < 0) {
		return ipc_send_or_drop(client, build_error_reply(0, "invalid request id"));
	}

	got_type = json_get_string(line, "type", type, sizeof(type));
	if (got_type <= 0) {
		return ipc_send_or_drop(client, build_error_reply(id, "missing request type"));
	}

	if (!strcmp(type, "get_state")) {
		char *snapshot = build_snapshot();
		reply = snapshot ? build_state_reply(id, snapshot) : NULL;
		free(snapshot);
		return ipc_send_or_drop(client, reply ? reply : build_error_reply(id, "failed to build state"));
	}

	if (!strcmp(type, "subscribe")) {
		char *snapshot = build_snapshot();
		client->subscribed = 1;
		reply = snapshot ? build_state_reply(id, snapshot) : NULL;
		free(snapshot);
		return ipc_send_or_drop(client, reply ? reply : build_error_reply(id, "failed to build state"));
	}

	if (!strcmp(type, "set_workspace")) {
		if (json_get_int(line, "workspace_id", &workspace_id) <= 0) {
			return ipc_send_or_drop(client, build_error_reply(id, "missing workspace_id"));
		}
		if (ipc_set_workspace_by_id((unsigned int)workspace_id) < 0) {
			return ipc_send_or_drop(client, build_error_reply(id, "unknown workspace"));
		}
		return ipc_send_or_drop(client, build_ok_reply(id));
	}

	if (!strcmp(type, "set_vout_focus")) {
		VirtualOutput *vout = resolve_vout(line, &error);
		if (!vout) {
			return ipc_send_or_drop(client, build_error_reply(id, error));
		}
		if (ipc_focus_virtual_output(vout) < 0) {
			return ipc_send_or_drop(client, build_error_reply(id, "failed to focus virtual output"));
		}
		return ipc_send_or_drop(client, build_ok_reply(id));
	}

	if (!strcmp(type, "move_workspace_to_vout")) {
		VirtualOutput *vout;
		Workspace *ws;

		if (json_get_int(line, "workspace_id", &workspace_id) <= 0) {
			return ipc_send_or_drop(client, build_error_reply(id, "missing workspace_id"));
		}
		ws = wsbyid((unsigned int)workspace_id);
		if (!ws) {
			return ipc_send_or_drop(client, build_error_reply(id, "unknown workspace"));
		}

		vout = resolve_vout(line, &error);
		if (!vout) {
			return ipc_send_or_drop(client, build_error_reply(id, error));
		}

		if (ipc_move_workspace_to_vout(ws, vout) < 0) {
			return ipc_send_or_drop(client, build_error_reply(id, "failed to move workspace"));
		}
		return ipc_send_or_drop(client, build_ok_reply(id));
	}

	return ipc_send_or_drop(client, build_error_reply(id, "unknown request type"));
}

static int
handle_client_buffer(IPCClient *client)
{
	char *line_start = client->buffer;
	char *newline;

	while ((newline = memchr(line_start, '\n', client->used - (size_t)(line_start - client->buffer)))) {
		size_t line_len = (size_t)(newline - line_start);
		if (line_len && line_start[line_len - 1] == '\r')
			line_len--;
		*newline = '\0';
		if (line_len && handle_request(client, line_start) < 0)
			return -1;
		line_start = newline + 1;
	}

	if (line_start != client->buffer) {
		size_t remaining = client->used - (size_t)(line_start - client->buffer);
		memmove(client->buffer, line_start, remaining);
		client->used = remaining;
	}
	return 0;
}

static int
ipc_client_ready(int fd, uint32_t mask, void *data)
{
	IPCClient *client = data;

	if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
		ipc_client_destroy(client);
		return 0;
	}

	for (;;) {
		ssize_t n = read(fd, client->buffer + client->used,
			sizeof(client->buffer) - client->used - 1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			ipc_client_destroy(client);
			return 0;
		}
		if (n == 0) {
			ipc_client_destroy(client);
			return 0;
		}
		client->used += (size_t)n;
		client->buffer[client->used] = '\0';
		if (handle_client_buffer(client) < 0)
			return 0;
		if (client->used == sizeof(client->buffer) - 1) {
			(void)ipc_send_or_drop(client, build_error_reply(0, "request too large"));
			return 0;
		}
	}

	return 0;
}

static void
ipc_client_destroy(IPCClient *client)
{
	if (!client)
		return;
	if (client->source)
		wl_event_source_remove(client->source);
	if (client->fd >= 0)
		close(client->fd);
	wl_list_remove(&client->link);
	free(client);
}

static int
ipc_listen_ready(int fd, uint32_t mask, void *data)
{
	(void)data;

	if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP))
		return 0;

	for (;;) {
		IPCClient *client;
		int client_fd = accept(fd, NULL, NULL);

		if (client_fd < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return 0;
		}

		if (fd_set_nonblock(client_fd) < 0) {
			close(client_fd);
			continue;
		}

		client = ecalloc(1, sizeof(*client));
		client->fd = client_fd;
		client->source = wl_event_loop_add_fd(event_loop, client_fd,
			WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP,
			ipc_client_ready, client);
		if (!client->source) {
			close(client_fd);
			free(client);
			continue;
		}
		wl_list_insert(&ipc_server.clients, &client->link);
	}

	return 0;
}

void
ipc_init(void)
{
	struct sockaddr_un addr;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

	if (ipc_server.listen_fd >= 0)
		return;
	if (!runtime_dir || !*runtime_dir)
		die("ipc: XDG_RUNTIME_DIR must be set");

	wl_list_init(&ipc_server.clients);
	if (snprintf(ipc_server.path, sizeof(ipc_server.path), "%s/vwl.sock", runtime_dir)
			>= (int)sizeof(ipc_server.path))
		die("ipc: socket path too long");

	ipc_server.listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ipc_server.listen_fd < 0)
		die("ipc: socket:");
	if (fd_set_nonblock(ipc_server.listen_fd) < 0)
		die("ipc: failed to set socket nonblocking");

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, ipc_server.path, sizeof(addr.sun_path) - 1);
	unlink(ipc_server.path);
	if (bind(ipc_server.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		die("ipc: bind:");
	if (listen(ipc_server.listen_fd, 16) < 0)
		die("ipc: listen:");

	ipc_server.listen_source = wl_event_loop_add_fd(event_loop, ipc_server.listen_fd,
		WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP, ipc_listen_ready, NULL);
	if (!ipc_server.listen_source)
		die("ipc: failed to add event source");

	setenv("VWL_SOCKET", ipc_server.path, 1);
}

void
ipc_finish(void)
{
	IPCClient *client, *tmp;

	wl_list_for_each_safe(client, tmp, &ipc_server.clients, link)
		ipc_client_destroy(client);

	if (ipc_server.listen_source) {
		wl_event_source_remove(ipc_server.listen_source);
		ipc_server.listen_source = NULL;
	}
	if (ipc_server.listen_fd >= 0) {
		close(ipc_server.listen_fd);
		ipc_server.listen_fd = -1;
	}
	if (ipc_server.path[0]) {
		unlink(ipc_server.path);
		ipc_server.path[0] = '\0';
	}
}

void
updateipc(void)
{
	IPCClient *client, *tmp;
	char *snapshot = NULL;
	char *event = NULL;
	bool have_subscribers = false;

	wl_list_for_each(client, &ipc_server.clients, link) {
		if (client->subscribed) {
			have_subscribers = true;
			break;
		}
	}

	if (have_subscribers) {
		snapshot = build_snapshot();
		if (snapshot)
			event = build_event(snapshot);
	}

	wl_list_for_each_safe(client, tmp, &ipc_server.clients, link) {
		if (client->subscribed && (!event || ipc_send_line(client, event) < 0))
			ipc_client_destroy(client);
	}

	free(event);
	free(snapshot);
	update_fullscreen_idle_inhibit();
}
