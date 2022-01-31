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
    int do_link_files;
    int do_show_number_of_disks;
    int do_recursive_search;
} ctx;

/*
 * To be able to fit files and present a disklist file_info stores the
 * size and the name of a file.
 */
struct file_info {
    off_t size;
    char *name;
};

static struct file_info *new_file_info(const char *name, off_t size) {
    struct file_info *file_info;

    file_info = xmalloc(sizeof(*file_info));
    file_info->name = xstrdup(name);
    file_info->size = size;

    return file_info;
}

static void free_file_info(void *file_info_ptr) {
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

static struct disk *new_disk(off_t size) {
    static size_t disk_id;
    struct disk *disk;

    disk = xmalloc(sizeof(*disk));
    disk->files = new_array();
    disk->free = size;
    disk->id = ++disk_id;

    return disk;
}

static void free_disk(void *disk_ptr) {
    struct disk *disk = disk_ptr;

    /*
     * NOTE: Files are shared with the files array so we don't use a
     * free function to clean them up here; we
     * would double free otherwise.
     */
    free_array(disk->files);
    free(disk);
}

static int add_file_to_disk(struct disk *disk, struct file_info *file_info) {
    if (disk->free - file_info->size < 0) {
        return 0;
    }

    add_to_array(disk->files, file_info);
    disk->free -= file_info->size;

    return 1;
}

static void print_separator(int length) {
    int i;

    for (i = 0; i < length; ++i) {
        putchar('-');
    }

    putchar('\n');
}

/*
 * Pretty print a disk and it's contents.
 */
static void print_disk(struct disk *disk) {
    char header[BUFSIZE];
    char *size_string;
    size_t file_nr;

    /* print a nice header */
    size_string = number_to_string(disk->free);
    sprintf(header, "Disk #%lu, %d%% (%s) free:", (unsigned long) disk->id,
            (int) (disk->free * 100 / ctx.disk_size), size_string);
    free(size_string);

    print_separator(strlen(header));
    printf("%s\n", header);
    print_separator(strlen(header));

    /* and the contents */
    for (file_nr = 0; file_nr < disk->files->size; ++file_nr) {
        struct file_info *file_info = disk->files->items[file_nr];

        size_string = number_to_string(file_info->size);
        printf("%10s %s\n", size_string, file_info->name);
        free(size_string);
    }

    putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void link_files_from_disk(struct disk *disk, char *destdir) {
    char *path;
    char *tmp;
    size_t file_nr;

    if (disk->id > 9999) {
        errx(1, "Number too big for format string.");
    }

    path = xmalloc(strlen(destdir) + 6);
    sprintf(path, "%s/%04lu", destdir, (unsigned long) disk->id);
    tmp = clean_path(path);
    free(path);
    path = tmp;

    for (file_nr = 0; file_nr < disk->files->size; ++file_nr) {
        struct file_info *file_info = disk->files->items[file_nr];
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
    size_t file_nr;

    qsort(files->items, files->size, sizeof(files->items[0]),
            by_size_descending);

    for (file_nr = 0; file_nr < files->size; ++file_nr) {
        struct file_info *file_info = files->items[file_nr];
        int added = false;
        size_t disk_nr;

        for (disk_nr = 0; disk_nr < disks->size; ++disk_nr) {
            struct disk *disk = disks->items[disk_nr];

            if (add_file_to_disk(disk, file_info)) {
                added = true;
                break;
            }
        }

        if (!added) {
            struct disk *disk;

            disk = new_disk(ctx.disk_size);
            if (!add_file_to_disk(disk, file_info)) {
                errx(1, "Can't fit file onto disk.");
            }

            add_to_array(disks, disk);
        }
    }
}

int collect_files(const char *filename, const struct stat *sb, int file_type,
        struct FTW *ftwbuf) {

    /* there might be access errors */
    if (file_type == FTW_NS || file_type == FTW_SLN || file_type == FTW_DNR) {
        err(1, "Can't access '%s'.", filename);
    }

    /* skip directories */
    if (file_type == FTW_D) {
        return 0;
    }

    /* skip subdirectories if not doing a recursive collect */
    if (!ctx.do_recursive_search && ftwbuf->level > 1) {
        return 0;
    }

    /* collect regular files which can fit on a disk */
    if (file_type == FTW_F) {
        struct file_info *file_info;

        if (sb->st_size > ctx.disk_size) {
            errx(1, "Can never fit '%s' (%s).", filename,
                    number_to_string(sb->st_size));
        }

        file_info = new_file_info(filename, sb->st_size);
        add_to_array(ctx.files, file_info);
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
    size_t disk_nr;

    while ((opt = getopt(argc, argv, "l:nrs:")) != -1) {
        switch (opt) {
            case 'l':
                destdir = clean_path(optarg);
                ctx.do_link_files = true;
                break;

            case 'n':
                ctx.do_show_number_of_disks = true;
                break;

            case 'r':
                ctx.do_recursive_search = true;
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

    ctx.files = new_array();

    for (arg = optind; arg < argc; ++arg) {
        nftw(argv[arg], collect_files, MAXFD, 0);
    }

    if (ctx.files->size == 0) {
        errx(1, "no files found.");
    }

    disks = new_array();
    fit_files(ctx.files, disks);

    /*
     * Be realistic about the number of disks to support, the helper
     * functions above assume a format string which will fit 4 digits.
     */
    if (disks->size > 9999) {
        errx(1, "Fitting takes too many (%lu) disks.", disks->size);
    }

    if (ctx.do_show_number_of_disks) {
        printf("%lu disk%s.\n", (unsigned long) disks->size,
                disks->size > 1 ? "s" : "");
        exit(EXIT_SUCCESS);
    }

    for (disk_nr = 0; disk_nr < disks->size; ++disk_nr) {
        struct disk *disk = disks->items[disk_nr];

        if (ctx.do_link_files) {
            link_files_from_disk(disk, destdir);
        } else {
            print_disk(disk);
        }
    }

    for_each_array_item(ctx.files, free_file_info);
    for_each_array_item(disks, free_disk);
    free_array(ctx.files);
    free_array(disks);

    if (ctx.do_link_files) {
        free(destdir);
    }

    return EXIT_SUCCESS;
}

