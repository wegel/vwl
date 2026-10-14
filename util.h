#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <stdio.h>

/* See LICENSE.dwm file for copyright and license details. */

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
int fd_set_nonblock(int fd);
void json_write_escaped(FILE *fp, const char *value);

#endif
