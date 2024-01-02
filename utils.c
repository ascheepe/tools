/*
 * Copyright (c) 2023 Axel Scheepers
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

#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>
#include <unistd.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

void die(const char *fmt, ...)
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

    exit(EXIT_FAILURE);
}

void *xcalloc(size_t nmemb, size_t size)
{
    void *ptr;

    ptr = calloc(nmemb, size);
    if (ptr == NULL) {
        die("calloc:");
    }

    return ptr;
}

void xlink(const char *src, const char *dst)
{
    if (link(src, dst) == -1) {
        die("Can't link '%s' to '%s':", src, dst);
    }
}

void *xmalloc(size_t size)
{
    void *ptr;

    ptr = malloc(size);
    if (ptr == NULL) {
        die("malloc:");
    }

    return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
    void *newptr;

    newptr = realloc(ptr, size);
    if (newptr == NULL) {
        die("realloc:");
    }

    return newptr;
}

char *xstrdup(const char *str)
{
    char *str_copy;
    size_t size;

    if (str == NULL) {
        return NULL;
    }

    size = strlen(str) + 1;
    str_copy = xmalloc(size);

    memcpy(str_copy, str, size);

    return str_copy;
}

#define KB 1000L
#define MB (KB * KB)
#define GB (MB * KB)
#define TB (GB * KB)

off_t string_to_number(const char *str)
{
    char *unit;
    off_t num;

    num = strtol(str, &unit, 10);
    if (unit == str) {
        die("Can't convert string '%s' to a number.", str);
    }

    if (*unit == '\0') {
        return num;
    }

    /* unit should be one char, not more */
    if (unit[1] == '\0') {
        switch (tolower(*unit)) {
            case 't':
                return num * TB;
            case 'g':
                return num * GB;
            case 'm':
                return num * MB;
            case 'k':
                return num * KB;
            case 'b':
                return num;
        }
    }

    die("Unknown unit: '%s'", unit);
    return 0;
}

char *number_to_string(double num)
{
    char number[BUFSIZE];
    char units[] = { 'b', 'K', 'M', 'G', 'T' };
    int i;

    for (i = 0; num > KB && i < (int) sizeof(units); ++i) {
        num /= KB;
    }

    sprintf(number, "%.*f%c", i == 0 ? 0 : 2, num, units[i]);

    return xstrdup(number);
}

#undef KB
#undef MB
#undef GB
#undef TB

char *clean_path(char *path)
{
    char *cleaned_path;
    char *path_buffer;
    char *new_path;

    path_buffer = new_path = xmalloc(strlen(path) + 1);
    while (*path != '\0') {
        if (*path == '/') {
            *new_path++ = *path++;

            while (*path == '/') {
                ++path;
            }
        } else {
            *new_path++ = *path++;
        }
    }

    if (new_path > (path_buffer + 1) && new_path[-1] == '/') {
        new_path[-1] = '\0';
    } else {
        *new_path = '\0';
    }

    cleaned_path = xstrdup(path_buffer);
    xfree(path_buffer);

    return cleaned_path;
}

static void makedir(char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {

        /* if path already exists it should be a directory */
        if (!S_ISDIR(st.st_mode)) {
            die("'%s' is not a directory.", path);
        }

        return;
    }

    if (mkdir(path, 0700) == -1) {
        die("Can't make directory '%s':", path);
    }
}

void makedirs(char *path)
{
    char *slash_position = path + 1;

    while ((slash_position = strchr(slash_position, '/')) != NULL) {
        *slash_position = '\0';
        makedir(path);
        *slash_position++ = '/';
    }

    makedir(path);
}
