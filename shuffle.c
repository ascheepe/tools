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

static const char *const usage_string = "\
usage:  shuffle [-p starting path] -e extension | -m media-type command\n\
\n\
options:\n\
  -p path        starts the search from this path.\n\
  -e extension   search for files with this extension.\n\
  -m media-type  search for files with this media type.\n\
  -v             show what's being done.\n\
  command        the command to execute for each file.\n\
\n";

#define _XOPEN_SOURCE 600
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <ftw.h>
#include <unistd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <magic.h>

#include "vector.h"
#include "utils.h"

static struct context {
	magic_t magic_cookie;
	char *mediatype;
	char *extension;
	char **command;
	int filename_index;
	int verbose;
	struct vector *files;
} ctx;

static int
collect(const char *fpath, const struct stat *st, int type, struct FTW *ftwbuf)
{
	int playable = FALSE;

	/* these parameters are unused */
	(void)st;
	(void)ftwbuf;

	/* skip non regular files */
	if (type != FTW_F)
		return 0;

	/* if both extension and media-type are set prefer extension search */
	if (ctx.extension != NULL) {
		const char *ext;

		ext = fpath + strlen(fpath) - strlen(ctx.extension);
		playable = (ext >= fpath &&
		    strcasecmp(ext, ctx.extension) == 0);
	} else if (ctx.mediatype != NULL) {
		const char *mediatype;

		mediatype = magic_file(ctx.magic_cookie, fpath);
		if (mediatype == NULL)
			die("collect: %s", magic_error(ctx.magic_cookie));

		playable = (strncmp(ctx.mediatype, mediatype,
		    strlen(ctx.mediatype)) == 0);
	} else
		die("Extension or media type is not set.");

	if (playable)
		vector_add(ctx.files, xstrdup(fpath));

	return 0;
}

static void
play_file(void *filenamep)
{
	char *filename = filenamep;

	switch (fork()) {
	case -1:
		die("Can't fork:");
		return;
	case 0:
		if (ctx.verbose)
			printf("Playing \"%s\".\n", filename);

		ctx.command[ctx.filename_index] = filename;
		execvp(ctx.command[0], (char *const *)ctx.command);
		die("Can't execute player:");
		break;
	default:
		wait(NULL);
		break;
	}
}

static void
init_magic(void)
{
	ctx.magic_cookie = magic_open(MAGIC_MIME);

	if (ctx.magic_cookie == NULL)
		die("Can't open libmagic.");

	if (magic_load(ctx.magic_cookie, NULL) == -1)
		die("%s.", magic_error(ctx.magic_cookie));
}

/* build a command from the arguments. The command starts
 * after the normal arguments.
 */
static void
build_command(int argc, char **argv, int argend)
{
	int len = argc - argend;
	int i;

	/* reserve for command + filename + NULL */
	ctx.command = xmalloc((len + 2) * sizeof(char *));

	for (i = argend; i < argc; ++i)
		ctx.command[i - argend] = argv[i];

	ctx.filename_index = len;

	ctx.command[ctx.filename_index] = NULL;
	ctx.command[ctx.filename_index + 1] = NULL;
}

static void
usage(void)
{
	fprintf(stderr, "%s", usage_string);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	char *path = NULL;
	int opt;

	/*
	 * GNU libc is not posix compliant and needs a + to stop
	 * getopt from parsing options after the last one otherwise
	 * it will parse the command's options as flags. (but you
	 * could stop that by prefixing the command with --).
	 */
#ifdef __GNU_LIBRARY__
	while ((opt = getopt(argc, argv, "+e:m:p:v")) != -1) {
#else
	while ((opt = getopt(argc, argv, "e:m:p:v")) != -1) {
#endif
		switch (opt) {
		case 'e':
			ctx.extension = optarg;
			break;
		case 'm':
			init_magic();
			ctx.mediatype = optarg;
			break;
		case 'p':
			path = realpath(optarg, NULL);

			if (path == NULL)
				die("Can't resolve starting path '%s'.",
				    optarg);
			break;
		case 'v':
			ctx.verbose = TRUE;
			break;
		}
	}

	/* extension or media-type must be set */
	if (ctx.extension == NULL && ctx.mediatype == NULL)
		usage();

	/* a command to run is mandatory */
	if (optind >= argc)
		usage();

	build_command(argc, argv, optind);

	if (ctx.verbose) {
		printf("Searching for files...");
		fflush(stdout);
	}

	ctx.files = vector_new();

	if (path == NULL)
		path = xstrdup(".");

	if (nftw(path, collect, MAXFD, FTW_PHYS) == -1)
		die("nftw:");

	free(path);

	if (ctx.magic_cookie != NULL)
		magic_close(ctx.magic_cookie);

	if (ctx.files->size == 0) {
		if (ctx.verbose)
			printf("no files found.\n");

		exit(1);
	}

	if (ctx.verbose)
		printf("%lu files found.\n", (ulong)ctx.files->size);

	vector_shuffle(ctx.files);
	vector_foreach(ctx.files, play_file);

	xfree(ctx.command);
	vector_foreach(ctx.files, free);
	vector_free(ctx.files);
	return EXIT_SUCCESS;
}
