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
	-p path		starts the search from this path.\n\
	-e extension	search for files with this extension.\n\
	-m media-type	search for files with this media type.\n\
	-v		show what's being done.\n\
	command		the command to execute for each file.\n\
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
#include <time.h>

#include <magic.h>

#include "array.h"
#include "utils.h"

/* libmagic state */
static magic_t magic_cookie;

/* search for this media type */
static char *media_type;

/* search for this file extension */
static char *extension;

/* the command to execute */
static char **command;

/* filename position in the command array */
static int filename_index;

/* show what's being done */
static int verbose;

/* playable files are stored in this array */
static struct array *files;

static int
collect_files(const char *filename, const struct stat *sb,
    int typeflag, struct FTW *ftwbuf)
{
	int playable = false;

	/* these parameters are unused */
	(void) sb;
	(void) ftwbuf;

	/* skip non regular files */
	if (typeflag != FTW_F)
		return 0;

	/* if both extension and media-type are set prefer extension search */
	if (extension != NULL)
		playable = string_ends_with(filename, extension);
	else if (media_type != NULL) {
		const char *file_type;

		file_type = magic_file(magic_cookie, filename);
		if (file_type == NULL)
			errx(1, "%s", magic_error(magic_cookie));

		playable = (strncmp(media_type, file_type,
		    strlen(media_type)) == 0);
	} else
		errx(1, "Extension or media type is not set.");

	if (playable)
		array_add(files, xstrdup(filename));

	return 0;
}

static void
play_file(void *filename_ptr)
{
	char *filename = filename_ptr;

	switch (fork()) {
	case -1:
		err(1, "Can't fork.");
		return;

	case 0:
		if (verbose)
			printf("Playing \"%s\".\n", filename);

		command[filename_index] = filename;
		execvp(command[0], (char *const *) command);
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
	magic_cookie = magic_open(MAGIC_MIME);

	if (magic_cookie == NULL)
		err(1, "Can't open libmagic.");

	if (magic_load(magic_cookie, NULL) == -1)
		errx(1, "%s.", magic_error(magic_cookie));
}

static void
build_command(int argc, char **argv, int argend)
{
	int i, cmdlen;

	/* reserve for command + filename + NULL */
	cmdlen = argc - argend;
	command = xmalloc((cmdlen + 2) * sizeof(char *));

	for (i = argend; i < argc; ++i)
		command[i - argend] = argv[i];

	filename_index = cmdlen;
	command[filename_index] = NULL;
	command[filename_index + 1] = NULL;
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
			extension = optarg;
			break;

		case 'm':
			init_magic();
			media_type = optarg;
			break;

		case 'p':
			path = realpath(optarg, NULL);

			if (path == NULL)
				errx(1, "Can't resolve starting path '%s'.",
				    optarg);

			break;

		case 'v':
			verbose = true;
			break;
		}
	}

	/* extension or media-type must be set */
	if (extension == NULL && media_type == NULL)
		usage();

	/* a command to run is mandatory */
	if (optind >= argc)
		usage();

	build_command(argc, argv, optind);

	if (verbose) {
		printf("Searching for files...");
		fflush(stdout);
	}

	files = array_new();

	if (path != NULL)
		nftw(path, collect_files, MAXFD, FTW_PHYS);
	else
		nftw(".", collect_files, MAXFD, FTW_PHYS);

	if (media_type)
		magic_close(magic_cookie);

	if (files->size == 0) {
		if (verbose)
			printf("no files found.\n");

		exit(1);
	}

	if (verbose)
		printf("%lu files found.\n", (unsigned long) files->size);

	array_shuffle(files);
	array_for_each(files, play_file);

	if (path != NULL)
		free(path);

	free(command);
	array_for_each(files, free);
	array_free(files);
	return EXIT_SUCCESS;
}
