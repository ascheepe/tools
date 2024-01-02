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
usage:  fit -s size [-l destination] [-nr] path [path ...]\n\
\n\
options:\n\
  -l destination Directory to link files into,\n\
                 if omitted just print the disks.\n\
  -n             Just show the number of disks it takes.\n\
  -r             Do a recursive search.\n\
  -s size        Disk size in k, m, g, or t.\n\
  -v             Print files which are being linked.\n\
  path           Path to the files to fit.\n\
\n";

#define _XOPEN_SOURCE 600
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <ftw.h>
#include <unistd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"
#include "utils.h"

enum flags {
    DO_LINK = 1,
    SHOW_ONLY = 2,
    RECURSIVE = 4,
    VERBOSE = 8
};

static struct context {
    off_t disk_size;
    struct vector *files;
    uint flags;
} ctx;

#define HAS_FLAG(flag) (ctx.flags & (flag))
#define SET_FLAG(flag) (ctx.flags |= (flag))

struct afile {
    off_t size;
    char *name;
};

static struct afile *afile_new(const char *name, off_t size)
{
    struct afile *afile;

    afile = xmalloc(sizeof(*afile));
    afile->name = xstrdup(name);
    afile->size = size;

    return afile;
}

static void afile_free(void *afilep)
{
    struct afile *afile = afilep;

    xfree(afile->name);
    xfree(afile);
}

struct disk {
    struct vector *files;
    off_t free;
    size_t id;
};

static struct disk *disk_new(off_t size)
{
    struct disk *disk;
    static size_t id;

    disk = xmalloc(sizeof(*disk));
    disk->files = vector_new();
    disk->free = size;
    disk->id = ++id;

    return disk;
}

static void disk_free(void *disk_ptr)
{
    struct disk *disk = disk_ptr;

    vector_foreach(disk->files, afile_free);
    vector_free(disk->files);
    xfree(disk);
}

static void hline(int length)
{
    while (length-- > 0) {
        putchar('-');
    }

    putchar('\n');
}

static void print_header(struct disk *disk)
{
    char header[BUFSIZE];
    char *disk_size;
    size_t header_length;

    disk_size = number_to_string(disk->free);
    header_length = sprintf(header, "Disk #%lu, %d%% (%s) free:",
                  (ulong) disk->id,
                  (int) (disk->free * 100 / ctx.disk_size), disk_size);
    xfree(disk_size);

    hline(header_length);
    printf("%s\n", header);
    hline(header_length);
}

/*
 * Pretty print a disk and it's contents.
 */
static void disk_print(struct disk *disk)
{
    size_t i;

    print_header(disk);
    for (i = 0; i < disk->files->size; ++i) {
        struct afile *afile = disk->files->items[i];
        char *file_size;

        file_size = number_to_string(afile->size);
        printf("%10s %s\n", file_size, afile->name);
        xfree(file_size);
    }

    putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void disk_link(struct disk *disk, char *destination_directory)
{
    size_t directory_length;
    size_t i;

    directory_length = strlen(destination_directory);
    for (i = 0; i < disk->files->size; ++i) {
        struct afile *afile = disk->files->items[i];
        char *link_destination;

        link_destination =
            xmalloc(directory_length + strlen(afile->name) + 2);
        sprintf(link_destination, "%s/%s", destination_directory,
                afile->name);
        xlink(afile->name, link_destination);
        if (HAS_FLAG(VERBOSE)) {
            printf("%s -> %s\n", afile->name, destination_directory);
        }
        xfree(link_destination);
    }
}

static int add_file(struct disk *disk, struct afile *afile)
{
    if (disk->free - afile->size < 0) {
        return FALSE;
    }

    vector_add(disk->files, afile);
    disk->free -= afile->size;

    return TRUE;
}

static int by_size_descending(const void *afile_a, const void *afile_b)
{
    struct afile *a = *((struct afile **) afile_a);
    struct afile *b = *((struct afile **) afile_b);

    return b->size - a->size;
}

/*
 * Fits files onto disks following a simple algorithm; first sort files
 * by size descending, then loop over the available disks for a fit. If
 * none can hold the file create a new disk containing it.  This will
 * rapidly fill disks while the smaller remaining files will usually
 * make a good final fit.
 */
static void fit(struct vector *files, struct vector *disks)
{
    size_t i;

    qsort(files->items, files->size, sizeof(files->items[0]),
          by_size_descending);

    for (i = 0; i < files->size; ++i) {
        struct afile *afile = files->items[i];
        int file_added = FALSE;
        size_t j;

        for (j = 0; j < disks->size; ++j) {
            struct disk *disk = disks->items[j];

            if (add_file(disk, afile)) {
                file_added = TRUE;
                break;
            }
        }

        if (!file_added) {
            struct disk *disk;

            disk = disk_new(ctx.disk_size);
            if (!add_file(disk, afile)) {
                die("add_file failed.");
            }

            vector_add(disks, disk);
        }
    }
}

static int collect_files(const char *fpath, const struct stat *st, int type,
                         struct FTW *ftwbuf)
{
    struct afile *afile;

    /* skip subdirectories if not doing a recursive collect_files */
    if (!HAS_FLAG(RECURSIVE) && ftwbuf->level > 1) {
        return 0;
    }

    /* there might be access errors */
    if (type == FTW_NS || type == FTW_SLN || type == FTW_DNR) {
        die("Can't access '%s':", fpath);
    }

    /* skip directories */
    if (type == FTW_D) {
        return 0;
    }

    /* we can only handle regular files */
    if (type != FTW_F) {
        die("'%s' is not a regular file.", fpath);
    }

    /* which are not too big to fit */
    if (st->st_size > ctx.disk_size)
        die("Can never fit '%s' (%s).", fpath, number_to_string(st->st_size));

    afile = afile_new(fpath, st->st_size);
    vector_add(ctx.files, afile);

    return 0;
}

static void usage(void)
{
    fprintf(stderr, "%s", usage_string);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    char *basedir = NULL;
    struct vector *disks = NULL;
    size_t i;
    int opt;

    while ((opt = getopt(argc, argv, "l:nrs:v")) != -1) {
        switch (opt) {
            case 'l':
                basedir = clean_path(optarg);
                SET_FLAG(DO_LINK);
                break;
            case 'n':
                SET_FLAG(SHOW_ONLY);
                break;
            case 'r':
                SET_FLAG(RECURSIVE);
                break;
            case 's':
                ctx.disk_size = string_to_number(optarg);
                break;
            case 'v':
                SET_FLAG(VERBOSE);
                break;
        }
    }

    /* A path argument and the size option is mandatory. */
    if (optind >= argc || ctx.disk_size <= 0) {
        usage();
    }

    ctx.files = vector_new();
    for (i = optind; (int) i < argc; ++i)
        if (nftw(argv[i], collect_files, MAXFD, 0) == -1) {
            die("nftw:");
        }

    if (ctx.files->size == 0) {
        die("no files found.");
    }

    disks = vector_new();
    fit(ctx.files, disks);

    /* There is room for 4 digits in the format string(s). */
    if (disks->size > 9999) {
        die("Fitting takes too many (%lu) disks.", disks->size);
    }

    if (HAS_FLAG(SHOW_ONLY)) {
        char *plural = (disks->size == 1 ? "disk" : "disks");

        printf("%lu %s.\n", (ulong) disks->size, plural);
        exit(EXIT_SUCCESS);
    }

    for (i = 0; i < disks->size; ++i) {
        struct disk *disk = disks->items[i];

        if (HAS_FLAG(DO_LINK)) {
            char *destination_directory;

            destination_directory = xmalloc(strlen(basedir) + 6);
            sprintf(destination_directory, "%s/%04lu", basedir,
                    (ulong) disk->id);
            makedirs(destination_directory);
            disk_link(disk, destination_directory);
            xfree(destination_directory);
        } else {
            disk_print(disk);
        }
    }

    vector_foreach(disks, disk_free);
    vector_free(ctx.files);
    vector_free(disks);

    if (HAS_FLAG(DO_LINK)) {
        xfree(basedir);
    }

    return EXIT_SUCCESS;
}
