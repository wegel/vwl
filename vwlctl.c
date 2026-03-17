#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ipc.h"
#include "util.h"

static void
usage(FILE *fp)
{
	fprintf(fp, "usage: vwlctl [--socket PATH] <command> [args]\n"
		    "\n"
		    "commands:\n"
		    "  get-state\n"
		    "  subscribe\n"
		    "  set-workspace WORKSPACE_ID\n"
		    "  set-vout-focus (--vout-id ID | --output NAME --vout NAME)\n"
		    "  move-workspace-to-vout WORKSPACE_ID (--vout-id ID | --output NAME --vout "
		    "NAME)\n");
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

static const char *
default_socket_path(char *buf, size_t buf_sz)
{
	const char *socket = getenv("VWL_SOCKET");
	const char *runtime_dir;

	if (socket && *socket)
		return socket;

	runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir || !*runtime_dir)
		die("vwlctl: XDG_RUNTIME_DIR must be set or VWL_SOCKET provided");
	if (snprintf(buf, buf_sz, "%s/%s", runtime_dir, VWL_IPC_SOCKET_NAME) >= (int)buf_sz)
		die("vwlctl: socket path too long");
	return buf;
}

static int
connect_socket(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		die("vwlctl: socket:");

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		die("vwlctl: connect %s:", path);

	return fd;
}

static char *
read_line(FILE *fp)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t n = getline(&line, &cap, fp);

	if (n < 0) {
		free(line);
		return NULL;
	}
	if (n > 0 && line[n - 1] == '\n')
		line[n - 1] = '\0';
	return line;
}

static int
json_reply_ok(const char *line)
{
	const char *p = strstr(line, "\"ok\":");

	if (!p)
		return -1;
	p += strlen("\"ok\":");
	if (!strncmp(p, "true", 4))
		return 1;
	if (!strncmp(p, "false", 5))
		return 0;
	return -1;
}

static void
append_vout_ref(FILE *fp, int *needs_comma, const char *output_name, const char *vout_name, const char *vout_id)
{
	if (vout_id) {
		fprintf(fp, "%s\"vout_id\":%s", *needs_comma ? "," : "", vout_id);
		*needs_comma = 1;
		return;
	}

	if (!output_name || !vout_name)
		die("vwlctl: virtual output requires --vout-id or --output NAME --vout NAME");

	fprintf(fp, "%s\"output\":", *needs_comma ? "," : "");
	json_write_escaped(fp, output_name);
	fputs(",\"vout_name\":", fp);
	json_write_escaped(fp, vout_name);
	*needs_comma = 1;
}

int
main(int argc, char *argv[])
{
	char socket_buf[PATH_MAX];
	const char *socket_path = NULL;
	const char *cmd;
	FILE *request_fp;
	FILE *reply_fp;
	char *request = NULL;
	size_t request_sz = 0;
	char *reply;
	int fd;
	int argi = 1;
	int status;

	while (argi < argc && !strcmp(argv[argi], "--socket")) {
		if (argi + 1 >= argc) {
			usage(stderr);
			return 1;
		}
		socket_path = argv[argi + 1];
		argi += 2;
	}

	if (argi >= argc) {
		usage(stderr);
		return 1;
	}

	cmd = argv[argi++];
	if (!socket_path)
		socket_path = default_socket_path(socket_buf, sizeof(socket_buf));

	request_fp = open_memstream(&request, &request_sz);
	if (!request_fp)
		die("vwlctl: open_memstream:");

	if (!strcmp(cmd, "get-state")) {
		fputs("{\"id\":1,\"type\":\"get_state\"}", request_fp);
	} else if (!strcmp(cmd, "subscribe")) {
		fputs("{\"id\":1,\"type\":\"subscribe\"}", request_fp);
	} else if (!strcmp(cmd, "set-workspace")) {
		if (argi >= argc)
			die("vwlctl: set-workspace requires WORKSPACE_ID");
		fprintf(request_fp, "{\"id\":1,\"type\":\"set_workspace\",\"workspace_id\":%s}", argv[argi]);
		argi++;
	} else if (!strcmp(cmd, "set-vout-focus") || !strcmp(cmd, "move-workspace-to-vout")) {
		const char *workspace_id = NULL;
		const char *output_name = NULL;
		const char *vout_name = NULL;
		const char *vout_id = NULL;
		int needs_comma = 0;

		if (!strcmp(cmd, "move-workspace-to-vout")) {
			if (argi >= argc)
				die("vwlctl: move-workspace-to-vout requires WORKSPACE_ID");
			workspace_id = argv[argi++];
		}

		while (argi < argc) {
			if (!strcmp(argv[argi], "--vout-id")) {
				if (argi + 1 >= argc)
					die("vwlctl: --vout-id requires a value");
				vout_id = argv[argi + 1];
				argi += 2;
			} else if (!strcmp(argv[argi], "--output")) {
				if (argi + 1 >= argc)
					die("vwlctl: --output requires a value");
				output_name = argv[argi + 1];
				argi += 2;
			} else if (!strcmp(argv[argi], "--vout")) {
				if (argi + 1 >= argc)
					die("vwlctl: --vout requires a value");
				vout_name = argv[argi + 1];
				argi += 2;
			} else {
				die("vwlctl: unknown argument %s", argv[argi]);
			}
		}

		fprintf(request_fp, "{\"id\":1,\"type\":");
		json_write_escaped(request_fp,
				!strcmp(cmd, "set-vout-focus") ? "set_vout_focus" : "move_workspace_to_vout");
		needs_comma = 1;
		if (workspace_id)
			fprintf(request_fp, ",\"workspace_id\":%s", workspace_id);
		append_vout_ref(request_fp, &needs_comma, output_name, vout_name, vout_id);
		fputc('}', request_fp);
	} else {
		usage(stderr);
		fclose(request_fp);
		free(request);
		return 1;
	}

	fclose(request_fp);

	fd = connect_socket(socket_path);
	if (write(fd, request, strlen(request)) < 0 || write(fd, "\n", 1) < 0)
		die("vwlctl: write:");
	free(request);

	reply_fp = fdopen(fd, "r+");
	if (!reply_fp)
		die("vwlctl: fdopen:");

	if (!strcmp(cmd, "subscribe")) {
		while ((reply = read_line(reply_fp))) {
			puts(reply);
			fflush(stdout);
			free(reply);
		}
		fclose(reply_fp);
		return 0;
	}

	reply = read_line(reply_fp);
	if (!reply)
		die("vwlctl: no reply from compositor");
	puts(reply);
	status = json_reply_ok(reply);
	free(reply);
	fclose(reply_fp);
	return status == 1 ? 0 : 1;
}
