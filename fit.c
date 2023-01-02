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
usage:  fit -s size [-l destination] [-nr] path [path ...]\n\
\n\
options:\n\
  -s size        disk size in k, m, g, or t.\n\
  -l destination directory to link files into,\n\
                 if omitted just print the disks.\n\
  -n             show the number of disks it takes.\n\
  path           path to the files to fit.\n\
\n";

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

static struct program_context {
    off_t disk_size;
    struct vector *files;
    int do_link_disks;
    int do_show_disk_count;
    int do_recursive_collect;
} ctx;

struct file_info {
    off_t size;
    char *name;
};

static struct file_info *file_info_new(const char *name, off_t size)
{
    struct file_info *file_info;

    file_info = xmalloc(sizeof(*file_info));
    file_info->name = xstrdup(name);
    file_info->size = size;

    return file_info;
}

static void file_info_free(void *file_info_ptr)
{
    struct file_info *file_info = file_info_ptr;

    xfree(file_info->name);
    xfree(file_info);
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

    /*
     * NOTE: Files are shared with the files vector so we don't use a
     * free function to clean them up here; we
     * would double free otherwise.
     */
    vector_free(disk->files);
    xfree(disk);
}

static int add_file(struct disk *disk, struct file_info *file_info)
{
    if (disk->free - file_info->size < 0) {
        return FALSE;
    }

    vector_add(disk->files, file_info);
    disk->free -= file_info->size;

    return TRUE;
}

static void separator(int length)
{
    while (length-- > 0) {
        putchar('-');
    }

    putchar('\n');
}

/*
 * Pretty print a disk and it's contents.
 */
static void disk_print(struct disk *disk)
{
    char header[BUFSIZE];
    char *size_string;
    size_t i;

    /* print a nice header */
    size_string = number_to_string(disk->free);
    sprintf(header, "Disk #%lu, %d%% (%s) free:",
            (unsigned long) disk->id,
            (int) (disk->free * 100 / ctx.disk_size), size_string);
    xfree(size_string);

    separator(strlen(header));
    printf("%s\n", header);
    separator(strlen(header));

    /* and the contents */
    for (i = 0; i < disk->files->size; ++i) {
        struct file_info *file_info = disk->files->items[i];

        size_string = number_to_string(file_info->size);
        printf("%10s %s\n", size_string, file_info->name);
        xfree(size_string);
    }

    putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void disk_link(struct disk *disk, char *destdir)
{
    char *tmp;
    char *path;
    size_t path_length;
    size_t i;

    if (disk->id > 9999) {
        die("Number too big for format string.");
    }

    tmp = xmalloc(strlen(destdir) + 6);
    sprintf(tmp, "%s/%04lu", destdir, (unsigned long) disk->id);
    path = clean_path(tmp);
    xfree(tmp);
    path_length = strlen(path);

    for (i = 0; i < disk->files->size; ++i) {
        struct file_info *file_info = disk->files->items[i];
        char *slashpos;
        char *destfile;

        destfile = xmalloc(path_length + strlen(file_info->name) + 2);
        sprintf(destfile, "%s/%s", path, file_info->name);
        slashpos = destfile + path_length;
        *slashpos = '\0';
        makedirs(destfile);
        *slashpos = '/';

        if (link(file_info->name, destfile) == -1) {
            die("Can't link '%s' to '%s':", file_info->name, destfile);
        }

        printf("%s -> %s\n", file_info->name, path);
        xfree(destfile);
    }

    xfree(path);
}

static int by_size_descending(const void *file_info_a,
                              const void *file_info_b)
{
    struct file_info *a = *((struct file_info **) file_info_a);
    struct file_info *b = *((struct file_info **) file_info_b);

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
        struct file_info *file_info = files->items[i];
        int added = FALSE;
        size_t j;

        for (j = 0; j < disks->size; ++j) {
            struct disk *disk = disks->items[j];

            if (add_file(disk, file_info)) {
                added = TRUE;
                break;
            }
        }

        if (!added) {
            struct disk *disk;

            disk = disk_new(ctx.disk_size);
            if (!add_file(disk, file_info)) {
                die("add_file failed.");
            }

            vector_add(disks, disk);
        }
    }
}

static int collect(const char *filename, const struct stat *st,
                   int filetype, struct FTW *ftwbuf)
{
    struct file_info *file_info;

    /* skip subdirectories if not doing a recursive collect */
    if (!ctx.do_recursive_collect && ftwbuf->level > 1) {
        return 0;
    }

    /* there might be access errors */
    if (filetype == FTW_NS || filetype == FTW_SLN || filetype == FTW_DNR) {
        die("Can't access '%s':", filename);
    }

    /* skip directories */
    if (filetype == FTW_D) {
        return 0;
    }

    /* we can only handle regular files */
    if (filetype != FTW_F) {
        die("'%s' is not a regular file.", filename);
    }

    /* which are not too big to fit */
    if (st->st_size > ctx.disk_size) {
        die("Can never fit '%s' (%s).",
            filename, number_to_string(st->st_size));
    }

    file_info = file_info_new(filename, st->st_size);
    vector_add(ctx.files, file_info);

    return 0;
}

static void usage(void)
{
    fprintf(stderr, "%s", usage_string);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    char *destdir = NULL;
    struct vector *disks;
    size_t i;
    int opt;

    while ((opt = getopt(argc, argv, "l:nrs:")) != -1) {
        switch (opt) {
            case 'l':
                destdir = clean_path(optarg);
                ctx.do_link_disks = TRUE;
                break;

            case 'n':
                ctx.do_show_disk_count = TRUE;
                break;

            case 'r':
                ctx.do_recursive_collect = TRUE;
                break;

            case 's':
                ctx.disk_size = string_to_number(optarg);
                break;
        }
    }

    /* A path argument and the size option is mandatory. */
    if (optind >= argc || ctx.disk_size <= 0) {
        usage();
    }

    ctx.files = vector_new();

    for (i = optind; (int) i < argc; ++i) {
        if (nftw(argv[i], collect, MAXFD, 0) == -1) {
            die("nftw:");
        }
    }

    if (ctx.files->size == 0) {
        die("no files found.");
    }

    disks = vector_new();
    fit(ctx.files, disks);

    /*
     * Be realistic about the number of disks to support, the helper
     * functions above assume a format string which will fit 4 digits.
     */
    if (disks->size > 9999) {
        die("Fitting takes too many (%lu) disks.", disks->size);
    }

    if (ctx.do_show_disk_count) {
        printf("%lu disk%s.\n",
               (unsigned long) disks->size, disks->size > 1 ? "s" : "");
        exit(EXIT_SUCCESS);
    }

    for (i = 0; i < disks->size; ++i) {
        struct disk *disk = disks->items[i];

        if (ctx.do_link_disks) {
            disk_link(disk, destdir);
        } else {
            disk_print(disk);
        }
    }

    vector_foreach(ctx.files, file_info_free);
    vector_foreach(disks, disk_free);
    vector_free(ctx.files);
    vector_free(disks);

    if (ctx.do_link_disks) {
        xfree(destdir);
    }

    return EXIT_SUCCESS;
}
