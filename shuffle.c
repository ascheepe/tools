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
usage:  shuffle [-p starting path] -e extension | -t media-type command\n\
\n\
options:\n\
  -p path        Starts the search from this path.\n\
  -e extension   Search for files with this extension.\n\
  -t media-type  Search for files with this media type.\n\
  -v             Show what's being done.\n\
  command        The command to run for each file.\n\
\n\
  The command to run has to include a % character which\n\
  is replaced by the filename.\n\
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
	magic_t mc;
	char *ftype;
	char *ext;
	char **cmd;
	int namepos;
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
	if (ctx.ext != NULL) {
		const char *ext;

		ext = fpath + strlen(fpath) - strlen(ctx.ext);
		playable = ext >= fpath && strcasecmp(ext, ctx.ext) == 0;
	} else if (ctx.ftype != NULL) {
		const char *ftype;

		ftype = magic_file(ctx.mc, fpath);
		if (ftype == NULL)
			die("collect: %s", magic_error(ctx.mc));

		playable = strncmp(ctx.ftype, ftype, strlen(ctx.ftype)) == 0;
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

		ctx.cmd[ctx.namepos] = filename;
		execvp(ctx.cmd[0], (char *const *)ctx.cmd);
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
	ctx.mc = magic_open(MAGIC_MIME);

	if (ctx.mc == NULL)
		die("Can't open libmagic.");

	if (magic_load(ctx.mc, NULL) == -1)
		die("%s.", magic_error(ctx.mc));
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
	ctx.cmd = xmalloc((len + 2) * sizeof(char *));

	ctx.namepos = -1;
	for (i = argend; i < argc; ++i) {
		if (strcmp(argv[i], "%") == 0)
			ctx.namepos = i - argend;
		ctx.cmd[i - argend] = argv[i];
	}

	if (ctx.namepos == -1)
		die("No %% character found in command to run.");

	ctx.cmd[len] = NULL;
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
	while ((opt = getopt(argc, argv, "+e:p:t:v")) != -1) {
#else
	while ((opt = getopt(argc, argv, "e:p:t:v")) != -1) {
#endif
		switch (opt) {
		case 'e':
			ctx.ext = optarg;
			break;
		case 't':
			init_magic();
			ctx.ftype = optarg;
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

	/* extension or filetype must be set */
	if (ctx.ext == NULL && ctx.ftype == NULL)
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

	if (ctx.mc != NULL)
		magic_close(ctx.mc);

	if (ctx.files->size == 0) {
		if (ctx.verbose)
			printf("no files found.\n");

		exit(1);
	}

	if (ctx.verbose)
		printf("%lu files found.\n", (ulong)ctx.files->size);

	vector_shuffle(ctx.files);
	vector_foreach(ctx.files, play_file);

	xfree(ctx.cmd);
	vector_foreach(ctx.files, free);
	vector_free(ctx.files);
	return EXIT_SUCCESS;
}
