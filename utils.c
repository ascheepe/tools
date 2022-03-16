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

void *
xcalloc(size_t nmemb, size_t size)
{
	void *ret;

	ret = calloc(nmemb, size);
	if (ret == NULL)
		err(1, NULL);

	return ret;
}

void *
xmalloc(size_t size)
{
	void *ret;

	ret = malloc(size);
	if (ret == NULL)
		errx(1, "Can't allocate memory.");

	return ret;
}

void *
xrealloc(void *ptr, size_t size)
{
	void *ret;

	ret = realloc(ptr, size);
	if (ret == NULL)
		errx(1, "Can't reallocate memory.");

	return ret;
}

char *
xstrdup(const char *str)
{
	size_t size;
	char *ret;

	size = strlen(str) + 1;
	ret = xmalloc(size);

	memcpy(ret, str, size);

	return ret;
}

#define KB 1000L
#define MB (KB * KB)
#define GB (MB * KB)
#define TB (GB * KB)

off_t
string_to_number(const char *str)
{
	char *unit;
	off_t num;

	num = strtol(str, &unit, 10);

	if (unit == str)
		errx(1, "Can't convert string '%s' to a number.", str);

	if (*unit == '\0')
		return num;

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

	errx(1, "Unknown unit: '%s'", unit);
	return 0;
}

char *
number_to_string(const double num)
{
	char str[BUFSIZE];

	if (num >= TB)
		sprintf(str, "%.2fT", num / TB);
	else if (num >= GB)
		sprintf(str, "%.2fG", num / GB);
	else if (num >= MB)
		sprintf(str, "%.2fM", num / MB);
	else if (num >= KB)
		sprintf(str, "%.2fK", num / KB);
	else
		sprintf(str, "%.0fB", num);

	return xstrdup(str);
}

#undef KB
#undef MB
#undef GB
#undef TB

char *
cleanpath(char *path)
{
	char *buf, *bufpos, *ret;

	buf = bufpos = xmalloc(strlen(path) + 1);

	while (*path != '\0') {
		if (*path == '/') {
			*bufpos++ = *path++;

			while (*path == '/')
				++path;
		} else
			*bufpos++ = *path++;
	}

	if (bufpos > (buf + 1) && bufpos[-1] == '/')
		bufpos[-1] = '\0';
	else
		*bufpos = '\0';

	ret = xstrdup(buf);
	free(buf);

	return ret;
}

static void
make_dir(char *path)
{
	struct stat st;

	if (stat(path, &st) == 0) {
		/* if path already exists it should be a directory */
		if (!S_ISDIR(st.st_mode))
			errx(1, "'%s' is not a directory.", path);

		return;
	}

	if (mkdir(path, 0700) == -1)
		err(1, "Can't make directory '%s'.", path);
}

void
makedirs(char *path)
{
	char *slashpos = path + 1;

	while ((slashpos = strchr(slashpos, '/')) != NULL) {
		*slashpos = '\0';
		make_dir(path);
		*slashpos++ = '/';
	}

	make_dir(path);
}

