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

static struct program_config
{
    magic_t         magic_cookie;
    char           *mediatype;
    char           *extension;
    char          **command;
    int             filename_index;
    int             verbose;
    struct vector  *files;
} cfg;

static int collect(const char *filename, const struct stat *st, int filetype,
                   struct FTW *ftwbuf)
{
    int playable = FALSE;

    /* these parameters are unused */
    (void) st;
    (void) ftwbuf;

    /* skip non regular files */
    if (filetype != FTW_F)
    {
        return 0;
    }

    /* if both extension and media-type are set prefer extension search */
    if (cfg.extension != NULL)
    {
        playable = strcasecmp(filename + strlen(filename)
                                       - strlen(cfg.extension),
                              cfg.extension) == 0;
    }
    else if (cfg.mediatype != NULL)
    {
        const char *mediatype = magic_file(cfg.magic_cookie, filename);

        if (mediatype == NULL)
        {
            errx(1, "%s", magic_error(cfg.magic_cookie));
        }

        playable = strncmp(cfg.mediatype, mediatype,
                           strlen(cfg.mediatype)) == 0;
    }
    else
    {
        errx(1, "Extension or media type is not set.");
    }

    if (playable)
    {
        vector_add(cfg.files, xstrdup(filename));
    }

    return 0;
}

static void playfile(void *filename_ptr)
{
    char *filename = filename_ptr;

    switch (fork())
    {
        case -1:
            err(1, "Can't fork");
            return;

        case 0:
            if (cfg.verbose)
            {
                printf("Playing \"%s\".\n", filename);
            }

            cfg.command[cfg.filename_index] = filename;
            execvp(cfg.command[0], (char *const *) cfg.command);
            err(1, "Can't execute player");
            break;

        default:
            wait(NULL);
            break;
    }
}

static void init_magic(void)
{
    cfg.magic_cookie = magic_open(MAGIC_MIME);

    if (cfg.magic_cookie == NULL)
    {
        errx(1, "Can't open libmagic.");
    }

    if (magic_load(cfg.magic_cookie, NULL) == -1)
    {
        errx(1, "%s.", magic_error(cfg.magic_cookie));
    }
}

/* build a command from the arguments. The command starts
 * after the normal arguments.
 */
static void build_command(int argc, char **argv, int command_start)
{
    int command_length  = argc - command_start;
    int argument_index;

    /* reserve for command + filename + NULL */
    cfg.command = xmalloc((command_length + 2) * sizeof(char *));

    for (argument_index = command_start;
         argument_index < argc;
         ++argument_index)
    {

        cfg.command[argument_index - command_start] = argv[argument_index];
    }

    cfg.filename_index = command_length;

    cfg.command[cfg.filename_index    ] = NULL;
    cfg.command[cfg.filename_index + 1] = NULL;
}

static void usage(void)
{
    fprintf(stderr, "%s", usage_string);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    char *path   = NULL;
    int   option;

    /*
     * GNU libc is not posix compliant and needs a + to stop
     * getopt from parsing options after the last one otherwise
     * it will parse the command's options as flags. (but you
     * could stop that by prefixing the command with --).
     */
#ifdef __GNU_LIBRARY__
    while ((option = getopt(argc, argv, "+e:m:p:v")) != -1)
#else
    while ((option = getopt(argc, argv, "e:m:p:v")) != -1)
#endif
    {
        switch (option)
        {
            case 'e':
                cfg.extension = optarg;
                break;

            case 'm':
                init_magic();
                cfg.mediatype = optarg;
                break;

            case 'p':
                path = realpath(optarg, NULL);

                if (path == NULL)
                {
                    errx(1, "Can't resolve starting path '%s'.", optarg);
                }

                break;

            case 'v':
                cfg.verbose = TRUE;
                break;
        }
    }

    /* extension or media-type must be set */
    if (cfg.extension == NULL && cfg.mediatype == NULL)
    {
        usage();
    }

    /* a command to run is mandatory */
    if (optind >= argc)
    {
        usage();
    }

    build_command(argc, argv, optind);

    if (cfg.verbose)
    {
        printf("Searching for files...");
        fflush(stdout);
    }

    cfg.files = vector_new();

    if (path != NULL)
    {
        nftw(path, collect, MAXFD, FTW_PHYS);
    }
    else
    {
        nftw(".", collect, MAXFD, FTW_PHYS);
    }

    if (cfg.magic_cookie != NULL)
    {
        magic_close(cfg.magic_cookie);
    }

    if (cfg.files->size == 0)
    {
        if (cfg.verbose)
        {
            printf("no files found.\n");
        }

        exit(1);
    }

    if (cfg.verbose)
    {
        printf("%lu files found.\n", (unsigned long) cfg.files->size);
    }

    vector_shuffle(cfg.files);
    vector_for_each(cfg.files, playfile);

    if (path != NULL)
    {
        free(path);
    }

    free(cfg.command);
    vector_for_each(cfg.files, free);
    vector_free(cfg.files);
    return EXIT_SUCCESS;
}

