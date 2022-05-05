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

static struct program_context {
    magic_t magic_cookie;
    char *mediatype;
    char *extension;
    char **command;
    int filename_index;
    int verbose;
    struct vector *files;
} context;

static int collect(const char *filename, const struct stat *st, int filetype,
    struct FTW *ftw_buffer) {

    int playable = false;

    /* these parameters are unused */
    (void) st;
    (void) ftw_buffer;

    /* skip non regular files */
    if (filetype != FTW_F) {
        return 0;
    }

    /* if both extension and media-type are set prefer extension search */
    if (context.extension != NULL)
        playable = strcasecmp(filename + strlen(filename) -
            strlen(context.extension), context.extension) == 0;
    else if (context.mediatype != NULL) {
        const char *mediatype = magic_file(context.magic_cookie, filename);

        if (mediatype == NULL) {
            errx(1, "%s", magic_error(context.magic_cookie));
        }

        playable = strncmp(context.mediatype, mediatype,
            strlen(context.mediatype)) == 0;
    } else {
        errx(1, "Extension or media type is not set.");
    }

    if (playable) {
        vector_add(context.files, xstrdup(filename));
    }

    return 0;
}

static void playfile(void *filename_ptr) {
    char *filename = filename_ptr;

    switch (fork()) {
        case -1:
            err(1, "Can't fork.");
            return;

        case 0:
            if (context.verbose) {
                printf("Playing \"%s\".\n", filename);
            }

            context.command[context.filename_index] = filename;
            execvp(context.command[0], (char *const *) context.command);
            err(1, "Can't execute player.");
            break;

        default:
            wait(NULL);
            break;
    }
}

static void init_magic(void) {
    context.magic_cookie = magic_open(MAGIC_MIME);

    if (context.magic_cookie == NULL) {
        err(1, "Can't open libmagic.");
    }

    if (magic_load(context.magic_cookie, NULL) == -1) {
        errx(1, "%s.", magic_error(context.magic_cookie));
    }
}

static void build_command(int argc, char **argv, int command_starting_position) {

    int command_length;
    int argument_nr;

    /* reserve for command + filename + NULL */
    command_length = argc - command_starting_position;
    context.command = xmalloc((command_length + 2) * sizeof(char *));

    for (argument_nr = command_starting_position; argument_nr < argc;
        ++argument_nr) {
        context.command[argument_nr - command_starting_position] =
            argv[argument_nr];
    }

    context.filename_index = command_length;
    context.command[context.filename_index] = NULL;
    context.command[context.filename_index + 1] = NULL;
}

static void usage(void) {
    fprintf(stderr, "%s", usage_string);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    char *path = NULL;
    int option;

    /*
     * GNU libc is not posix compliant and needs a + to stop
     * getopt from parsing options after the last one otherwise
     * it will parse the command's options as flags. (but you
     * could stop that by prefixing the command with --).
     */
#ifdef __GNU_LIBRARY__
    while ((option = getopt(argc, argv, "+e:m:p:v")) != -1) {
#else
    while ((option = getopt(argc, argv, "e:m:p:v")) != -1) {
#endif
        switch (option) {
            case 'e':
                context.extension = optarg;
                break;

            case 'm':
                init_magic();
                context.mediatype = optarg;
                break;

            case 'p':
                path = realpath(optarg, NULL);

                if (path == NULL) {
                    errx(1, "Can't resolve starting path '%s'.", optarg);
                }

                break;
            case 'v':
                context.verbose = true;
                break;
        }
    }

    /* extension or media-type must be set */
    if (context.extension == NULL && context.mediatype == NULL) {
        usage();
    }

    /* a command to run is mandatory */
    if (optind >= argc) {
        usage();
    }

    build_command(argc, argv, optind);

    if (context.verbose) {
        printf("Searching for files...");
        fflush(stdout);
    }

    context.files = vector_new();

    if (path != NULL) {
        nftw(path, collect, MAXFD, FTW_PHYS);
    } else {
        nftw(".", collect, MAXFD, FTW_PHYS);
    }

    if (context.mediatype) {
        magic_close(context.magic_cookie);
    }

    if (context.files->size == 0) {
        if (context.verbose) {
            printf("no files found.\n");
        }

        exit(1);
    }

    if (context.verbose) {
        printf("%lu files found.\n", (unsigned long) context.files->size);
    }

    vector_shuffle(context.files);
    vector_for_each(context.files, playfile);

    if (path != NULL) {
        free(path);
    }

    free(context.command);
    vector_for_each(context.files, free);
    vector_free(context.files);
    return EXIT_SUCCESS;
}

