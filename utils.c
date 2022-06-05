/*
 * Copyright (c) 2021 Axel Scheepers
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

void *xcalloc(size_t nmemb, size_t size) {
    void *result = calloc(nmemb, size);

    if (result == NULL) {
        errx(1, "calloc: out of memory.");
    }

    return result;
}

void *xmalloc(size_t size) {
    void *result = malloc(size);

    if (result == NULL) {
        errx(1, "malloc: out of memory.");
    }

    return result;
}

void *xrealloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size);

    if (result == NULL) {
        errx(1, "realloc: out of memory.");
    }

    return result;
}

char *xstrdup(const char *string) {
    char *result;
    size_t size;

    if (string == NULL) {
        return NULL;
    }

    size = strlen(string) + 1;
    result = xmalloc(size);

    memcpy(result, string, size);

    return result;
}

#define KB 1000L
#define MB (KB * KB)
#define GB (MB * KB)
#define TB (GB * KB)

off_t string_to_number(const char *string) {
    char *unit = NULL;
    off_t number = strtol(string, &unit, 10);

    if (unit == string) {
        errx(1, "Can't convert string '%s' to a number.", string);
    }

    if (*unit == '\0') {
        return number;
    }

    /* unit should be one char, not more */
    if (unit[1] == '\0') {
        switch (tolower(*unit)) {
            case 't':
                return number * TB;

            case 'g':
                return number * GB;

            case 'm':
                return number * MB;

            case 'k':
                return number * KB;

            case 'b':
                return number;
        }
    }

    errx(1, "Unknown unit: '%s'", unit);
    return 0;
}

char *number_to_string(const double number) {
    char string[BUFSIZE];

    if (number >= TB) {
        sprintf(string, "%.2fT", number / TB);
    } else if (number >= GB) {
        sprintf(string, "%.2fG", number / GB);
    } else if (number >= MB) {
        sprintf(string, "%.2fM", number / MB);
    } else if (number >= KB) {
        sprintf(string, "%.2fK", number / KB);
    } else {
        sprintf(string, "%.0fB", number);
    }

    return xstrdup(string);
}

#undef KB
#undef MB
#undef GB
#undef TB

char *clean_path(char *path) {
    char *buf = xmalloc(strlen(path) + 1);
    char *bufpos = buf;
    char *result = NULL;

    while (*path != '\0') {
        if (*path == '/') {
            *bufpos++ = *path++;

            while (*path == '/') {
                ++path;
            }
        } else {
            *bufpos++ = *path++;
        }
    }

    if (bufpos > (buf + 1) && bufpos[-1] == '/') {
        bufpos[-1] = '\0';
    } else {
        *bufpos = '\0';
    }

    result = xstrdup(buf);
    free(buf);

    return result;
}

static void make_dir(char *path) {
    struct stat st;

    if (stat(path, &st) == 0) {

        /* if path already exists it should be a directory */
        if (!S_ISDIR(st.st_mode)) {
            errx(1, "'%s' is not a directory.", path);
        }

        return;
    }

    if (mkdir(path, 0700) == -1) {
        err(1, "Can't make directory '%s'.", path);
    }
}

void make_dirs(char *path) {
    char *slash_position = path + 1;

    while ((slash_position = strchr(slash_position, '/')) != NULL) {
        *slash_position = '\0';
        make_dir(path);
        *slash_position++ = '/';
    }

    make_dir(path);
}

