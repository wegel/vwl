/* See LICENSE.dwm file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "util.h"

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}

int
fd_set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		perror("fcntl(F_GETFL):");
		return -1;
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		perror("fcntl(F_SETFL):");
		return -1;
	}

	return 0;
}

void
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
