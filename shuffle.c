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
  The command to run can include a % character which\n\
  is replaced by the filename. If this is omitted\n\
  the filename is appended to the command.\n\
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

	char *file_type;
	char *extension;

	char **command;
	int filename_position;

	int verbose;

	struct vector *files;
} ctx;

static int
collect_files(const char *fpath, const struct stat *st, int type,
    struct FTW *ftwbuf)
{
	int is_playable = FALSE;

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
		is_playable = ext >= fpath
		    && strcasecmp(ext, ctx.extension) == 0;
	} else if (ctx.file_type != NULL) {
		const char *file_type;

		file_type = magic_file(ctx.magic_cookie, fpath);
		if (file_type == NULL)
			die("collect_files: %s",
			    magic_error(ctx.magic_cookie));

		is_playable = strncmp(ctx.file_type, file_type,
		    strlen(ctx.file_type)) == 0;
	} else
		die("Extension or media type is not set.");

	if (is_playable)
		vector_add(ctx.files, xstrdup(fpath));

	return 0;
}

static void
play_file(void *filename_ptr)
{
	char *filename = filename_ptr;

	switch (fork()) {
	case -1:
		die("Can't fork:");
		return;
	case 0:
		if (ctx.verbose)
			printf("Playing \"%s\".\n", filename);

		ctx.command[ctx.filename_position] = filename;
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

/*
 * Build a command from the arguments. The command starts
 * after the normal arguments.
 */
static void
build_command(int argc, char **argv, int arg_end)
{
	int cmdlen = argc - arg_end;
	int i;

	/* + 2 in case we need to append the filename */
	ctx.command = xcalloc(cmdlen + 2, sizeof(char *));
	ctx.filename_position = -1;

	for (i = arg_end; i < argc; ++i) {
		int pos = i - arg_end;

		if (strcmp(argv[i], "%") == 0)
			ctx.filename_position = pos;
		ctx.command[pos] = argv[i];
	}

	/* If no % found append filename. */
	if (ctx.filename_position == -1)
		ctx.filename_position = cmdlen;
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
	char *starting_path = NULL;
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
			ctx.extension = optarg;
			break;
		case 't':
			init_magic();
			ctx.file_type = optarg;
			break;
		case 'p':
			starting_path = realpath(optarg, NULL);
			if (starting_path == NULL)
				die("Can't resolve '%s'.", optarg);
			break;
		case 'v':
			ctx.verbose = TRUE;
			break;
		}
	}

	/* extension or filetype must be set */
	if (ctx.extension == NULL && ctx.file_type == NULL)
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

	if (starting_path == NULL)
		starting_path = xstrdup(".");

	if (nftw(starting_path, collect_files, MAXFD, FTW_PHYS) == -1)
		die("nftw:");

	free(starting_path);

	if (ctx.magic_cookie != NULL)
		magic_close(ctx.magic_cookie);

	if (ctx.files->size == 0) {
		if (ctx.verbose)
			printf("no files found.\n");

		exit(1);
	}

	if (ctx.verbose)
		printf("%lu files found.\n", (ulong) ctx.files->size);

	vector_shuffle(ctx.files);
	vector_foreach(ctx.files, play_file);

	xfree(ctx.command);
	vector_foreach(ctx.files, free);
	vector_free(ctx.files);
	return EXIT_SUCCESS;
}
