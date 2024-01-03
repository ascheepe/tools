/*
 * Copyright (c) 2024 Axel Scheepers
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stdlib.h>

/* buffer big enough for storing the header string */
#define BUFSIZE 1024

/* maximum number of file descriptors ftw will use */
#define MAXFD 32

#define xfree(p) do { free(p); (p) = NULL; } while (0)

enum { FALSE, TRUE };

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

void die(const char *, ...);
void *xcalloc(size_t, size_t);
void xlink(const char *, const char *);
void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
off_t string_to_number(const char *);
char *number_to_string(double);
char *clean_path(char *);
void make_directories(char *);

#endif
