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

#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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
    void *new_ptr;

    new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        die("realloc:");
    }

    return new_ptr;
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
    off_t number;

    number = strtol(str, &unit, 10);
    if (unit == str) {
        die("Can't convert string '%s' to a number.", str);
    }

    if (*unit == '\0') {
        return number;
    }

    /* unit should be one char, not more */
    if (unit[1] == '\0') {
        switch (tolower(*unit)) {
            case 't': return number * TB;
            case 'g': return number * GB;
            case 'm': return number * MB;
            case 'k': return number * KB;
            case 'b': return number;
        }
    }

    die("Unknown unit: '%s'", unit);
    return 0;
}

char *number_to_string(double number)
{
    char str[BUFSIZE];
    char units[] = { 'b', 'K', 'M', 'G', 'T' };
    int i;

    for (i = 0; number > KB && i < (int) sizeof(units); ++i) {
        number /= KB;
    }

    sprintf(str, "%.*f%c", i == 0 ? 0 : 2, number, units[i]);

    return xstrdup(str);
}

#undef KB
#undef MB
#undef GB
#undef TB

char *clean_path(char *path)
{
    char *path_buffer;
    char *buffer_position;
    char *cleaned_path;

    path_buffer = xmalloc(strlen(path) + 1);
    buffer_position = path_buffer;

    /* Replace repeating slash characters with a single one. */
    while (*path != '\0') {
        if (*path == '/') {
            *buffer_position++ = *path++;

            while (*path == '/') {
                ++path;
            }
        } else {
            *buffer_position++ = *path++;
        }
    }

    /* Strip the last slash if it's not the only character. */
    if ((buffer_position > (path_buffer + 1))
            && (buffer_position[-1] == '/')) {
        buffer_position[-1] = '\0';
    } else {
        *buffer_position = '\0';
    }

    cleaned_path = xstrdup(path_buffer);
    xfree(path_buffer);

    return cleaned_path;
}

static void xmkdir(const char *path, mode_t mode)
{
    struct stat st;

    /* If the path already exists.. */
    if (stat(path, &st) == 0) {
        mode_t path_mode = st.st_mode & 07777;

        /* it should be a directory.. */
        if (!S_ISDIR(st.st_mode)) {
            die("'%s' is not a directory.", path);
        }

        /* and have correct permissions. */
        if (path_mode != mode) {
            die("'%s' has invalid permissions %o, should be %o.",
                path, path_mode, mode);
        }
    } else {

        /* Otherwise, create it. */
        if (mkdir(path, mode) == -1) {
            die("Can't make directory '%s':", path);
        }
    }
}

void make_directories(char *path)
{
    char *slash_position = path;
    mode_t directory_mode = 0700;

    while ((slash_position = strchr(++slash_position, '/')) != NULL) {
        *slash_position = '\0';
        xmkdir(path, directory_mode);
        *slash_position = '/';
    }

    xmkdir(path, directory_mode);
}
