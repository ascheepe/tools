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
usage:  fit -s size [-l destdir] [-nr] path [path ...]\n\
\n\
options:\n\
  -s size disk  size in k, m, g, or t.\n\
  -l destdir    directory to link files into,\n\
                if omitted just print the disks.\n\
  -n            show the number of disks it takes.\n\
  path          path to the files to fit.\n\
\n";

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <err.h>
#include <ftw.h>
#include <unistd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "utils.h"

static struct program_context {
    off_t disk_size;
    struct array *files;
    int lflag;
    int nflag;
    int rflag;
} ctx;

/*
 * To be able to fit files and present a disklist file_info stores the
 * size and the name of a file.
 */
struct file_info {
    off_t size;
    char *name;
};

static struct file_info *file_info_new(char *name, off_t size) {
    struct file_info *file_info;

    file_info = xmalloc(sizeof(*file_info));
    file_info->name = name;
    file_info->size = size;

    return file_info;
}

static void file_info_free(void *file_info_ptr) {
    struct file_info *file_info = file_info_ptr;

    free(file_info->name);
    free(file_info);
}

/*
 * A disk with 'free' free space contains a array of files. It's id
 * is an incrementing number which doubles as the total number of
 * disks made.
 */
struct disk {
    struct array *files;
    off_t free;
    size_t id;
};

static struct disk *disk_new(off_t size) {
    static size_t disk_id;
    struct disk *disk;

    disk = xmalloc(sizeof(*disk));
    disk->files = array_new();
    disk->free = size;
    disk->id = ++disk_id;

    return disk;
}

static void disk_free(void *disk_ptr) {
    struct disk *disk = disk_ptr;

    /*
     * NOTE: Files are shared with the files array so we don't use a
     * free function to clean them up here; we
     * would double free otherwise.
     */
    array_free(disk->files);
    free(disk);
}

static void hline(int len) {
    int i;

    for (i = 0; i < len; ++i) {
        putchar('-');
    }

    putchar('\n');
}

/*
 * Pretty print a disk and it's contents.
 */
static void disk_print(struct disk *disk) {
    char header[BUFSIZE];
    char *size_string;
    size_t i;

    /* print a nice header */
    size_string = number_to_string(disk->free);
    sprintf(header, "Disk #%lu, %d%% (%s) free:", (unsigned long) disk->id,
            (int) (disk->free * 100 / ctx.disk_size), size_string);
    free(size_string);

    hline(strlen(header));
    printf("%s\n", header);
    hline(strlen(header));

    /* and the contents */
    for (i = 0; i < disk->files->size; ++i) {
        struct file_info *file_info = disk->files->items[i];

        size_string = number_to_string(file_info->size);
        printf("%10s %s\n", size_string, file_info->name);
        free(size_string);
    }

    putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void disk_link(struct disk *disk, char *destdir) {
    char *path;
    char *tmp;
    size_t i;

    if (disk->id > 9999) {
        errx(1, "Number too big for format string.");
    }

    path = xmalloc(strlen(destdir) + 6);
    sprintf(path, "%s/%04lu", destdir, (unsigned long) disk->id);
    tmp = clean_path(path);
    free(path);
    path = tmp;

    for (i = 0; i < disk->files->size; ++i) {
        struct file_info *file_info = disk->files->items[i];
        char *slashpos;
        char *destfile;

        destfile = xmalloc(strlen(path) + strlen(file_info->name) + 2);
        sprintf(destfile, "%s/%s", path, file_info->name);
        slashpos = strrchr(destfile, '/');
        *slashpos = '\0';
        make_dirs(destfile);
        *slashpos = '/';

        if (link(file_info->name, destfile) == -1) {
            err(1, "Can't link '%s' to '%s'.", file_info->name, destfile);
        }

        printf("%s -> %s\n", file_info->name, path);
        free(destfile);
    }

    free(path);
}

static int by_size_descending(const void *file_info_a,
        const void *file_info_b) {

    struct file_info *a = *((struct file_info **) file_info_a);
    struct file_info *b = *((struct file_info **) file_info_b);

    /* order by size, descending */
    return b->size - a->size;
}

/*
 * Fits files onto disks following a simple algorithm; first sort files
 * by size descending, then loop over the available disks for a fit. If
 * none can hold the file create a new disk containing it.  This will
 * rapidly fill disks while the smaller remaining files will usually
 * make a good final fit.
 */
static void fit_files(struct array *files, struct array *disks) {
    size_t i;

    qsort(files->items, files->size, sizeof(files->items[0]),
            by_size_descending);

    for (i = 0; i < files->size; ++i) {
        struct file_info *file_info = files->items[i];
        int added = false;
        size_t j;

        for (j = 0; j < disks->size; ++j) {
            struct disk *disk = disks->items[j];

            if (disk->free - file_info->size >= 0) {
                array_add(disk->files, file_info);
                disk->free -= file_info->size;
                added = true;
                break;
            }
        }

        if (!added) {
            struct disk *disk;

            disk = disk_new(ctx.disk_size);
            array_add(disk->files, file_info);
            disk->free -= file_info->size;
            array_add(disks, disk);
        }
    }
}

int collect_files(const char *filename, const struct stat *sb, int typeflag,
        struct FTW *ftwbuf) {

    /* there might be access errors */
    if (typeflag == FTW_NS || typeflag == FTW_SLN || typeflag == FTW_DNR) {
        err(1, "Can't access '%s'.", filename);
    }

    /* skip directories */
    if (typeflag == FTW_D) {
        return 0;
    }

    /* skip subdirectories if not doing a recursive collect */
    if (!ctx.rflag && ftwbuf->level > 1) {
        return 0;
    }

    /* collect regular files which can fit on a disk */
    if (typeflag == FTW_F) {
        struct file_info *file_info;

        if (sb->st_size > ctx.disk_size) {
            errx(1, "Can never fit '%s' (%s).", filename,
                    number_to_string(sb->st_size));
        }

        file_info = file_info_new(xstrdup(filename), sb->st_size);
        array_add(ctx.files, file_info);
    } else {
        err(1, "'%s' is not a regular file.", filename);
    }

    return 0;
}

static void usage(void) {
    fprintf(stderr, "%s", usage_string);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    char *destdir = NULL;
    struct array *disks;
    int arg, opt;
    size_t i;

    while ((opt = getopt(argc, argv, "l:nrs:")) != -1) {
        switch (opt) {
            case 'l':
                destdir = clean_path(optarg);
                ctx.lflag = true;
                break;

            case 'n':
                ctx.nflag = true;
                break;

            case 'r':
                ctx.rflag = true;
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

    ctx.files = array_new();

    for (arg = optind; arg < argc; ++arg) {
        nftw(argv[arg], collect_files, MAXFD, 0);
    }

    if (ctx.files->size == 0) {
        errx(1, "no files found.");
    }

    disks = array_new();
    fit_files(ctx.files, disks);

    /*
     * Be realistic about the number of disks to support, the helper
     * functions above assume a format string which will fit 4 digits.
     */
    if (disks->size > 9999) {
        errx(1, "Fitting takes too many (%lu) disks.", disks->size);
    }

    if (ctx.nflag) {
        printf("%lu disk%s.\n", (unsigned long) disks->size,
                disks->size > 1 ? "s" : "");
        exit(EXIT_SUCCESS);
    }

    for (i = 0; i < disks->size; ++i) {
        struct disk *disk = disks->items[i];

        if (ctx.lflag) {
            disk_link(disk, destdir);
        } else {
            disk_print(disk);
        }
    }

    array_for_each(ctx.files, file_info_free);
    array_for_each(disks, disk_free);
    array_free(ctx.files);
    array_free(disks);

    if (ctx.lflag) {
        free(destdir);
    }

    return EXIT_SUCCESS;
}

