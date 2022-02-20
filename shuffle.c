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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <ftw.h>
#include <unistd.h>

#include <err.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <magic.h>

#include "vector.h"
#include "utils.h"

static struct {
	magic_t mcookie;
	char *mediatype;
	char *ext;
	char **cmd;
	int argpos;
	int verbose;
	struct vector *files;
} ctx;

static int
collect(const char *filename, const struct stat *st, int filetype,
    struct FTW *ftwbuf)
{
	int playable = false;

	/* these parameters are unused */
	(void) st;
	(void) ftwbuf;

	/* skip non regular files */
	if (filetype != FTW_F)
		return 0;

	/* if both extension and media-type are set prefer extension search */
	if (ctx.ext != NULL)
		playable = strcasecmp(filename + strlen(filename) -
		    strlen(ctx.ext), ctx.ext) == 0;
	else if (ctx.mediatype != NULL) {
		const char *mediatype;

		mediatype = magic_file(ctx.mcookie, filename);
		if (mediatype == NULL)
			errx(1, "%s", magic_error(ctx.mcookie));

		playable =
		    strncmp(ctx.mediatype, mediatype,
		    strlen(ctx.mediatype)) == 0;
	} else
		errx(1, "Extension or media type is not set.");

	if (playable)
		vector_add(ctx.files, xstrdup(filename));

	return 0;
}

static void
play_file(void *pfilename)
{
	char *filename = pfilename;

	switch (fork()) {
	case -1:
		err(1, "Can't fork.");
		return;
	case 0:
		if (ctx.verbose)
			printf("Playing \"%s\".\n", filename);

		ctx.cmd[ctx.argpos] = filename;
		execvp(ctx.cmd[0], (char *const *) ctx.cmd);
		err(1, "Can't execute player.");
		break;
	default:
		wait(NULL);
		break;
	}
}

static void
init_magic(void)
{
	ctx.mcookie = magic_open(MAGIC_MIME);

	if (ctx.mcookie == NULL)
		err(1, "Can't open libmagic.");

	if (magic_load(ctx.mcookie, NULL) == -1)
		errx(1, "%s.", magic_error(ctx.mcookie));
}

static void
build_command(int argc, char **argv, int cmdstart)
{
	int cmdlen;
	int i;

	/* reserve for command + filename + NULL */
	cmdlen = argc - cmdstart;
	ctx.cmd = xmalloc((cmdlen + 2) * sizeof(char *));

	for (i = cmdstart; i < argc; ++i)
		ctx.cmd[i - cmdstart] = argv[i];

	ctx.argpos = cmdlen;
	ctx.cmd[ctx.argpos] = NULL;
	ctx.cmd[ctx.argpos + 1] = NULL;
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
			ctx.ext = optarg;
			break;
		case 'm':
			init_magic();
			ctx.mediatype = optarg;
			break;
		case 'p':
			path = realpath(optarg, NULL);

			if (path == NULL)
				errx(1, "Can't resolve starting path '%s'.",
				    optarg);

			break;
		case 'v':
			ctx.verbose = true;
			break;
		}
	}

	/* extension or media-type must be set */
	if (ctx.ext == NULL && ctx.mediatype == NULL)
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

	if (path != NULL)
		nftw(path, collect, MAXFD, FTW_PHYS);
	else
		nftw(".", collect, MAXFD, FTW_PHYS);

	if (ctx.mediatype)
		magic_close(ctx.mcookie);

	if (ctx.files->size == 0) {
		if (ctx.verbose)
			printf("no files found.\n");

		exit(1);
	}

	if (ctx.verbose)
		printf("%lu files found.\n", (unsigned long) ctx.files->size);

	vector_shuffle(ctx.files);
	vector_foreach(ctx.files, play_file);

	if (path != NULL)
		free(path);

	free(ctx.cmd);
	vector_foreach(ctx.files, free);
	vector_free(ctx.files);
	return EXIT_SUCCESS;
}

