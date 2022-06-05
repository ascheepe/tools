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
#include <err.h>
#include <ftw.h>
#include <unistd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"
#include "utils.h"

static struct program_config {
    off_t disk_size;
    struct vector *files;
    int do_link_disks;
    int do_show_disk_count;
    int do_recursive_collect;
} cfg;

/*
 * To be able to fit files and present a disklist file stores the
 * size and the name of a file.
 */
struct file {
    off_t size;
    char *name;
};

static struct file *file_new(const char *name, off_t size) {
    struct file *file = xmalloc(sizeof(*file));

    file->name = xstrdup(name);
    file->size = size;

    return file;
}

static void file_free(void *file_ptr) {
    struct file *file = file_ptr;

    free(file->name);
    free(file);
}

/*
 * A disk with 'free' free space contains a vector of files. It's id
 * is an incrementing number which doubles as the total number of
 * disks made.
 */
struct disk {
    struct vector *files;
    off_t free;
    size_t id;
};

static struct disk *disk_new(off_t size) {
    static size_t id = 0;
    struct disk *disk = xmalloc(sizeof(*disk));

    disk->files = vector_new();
    disk->free = size;
    disk->id = ++id;

    return disk;
}

static void disk_free(void *disk_ptr) {
    struct disk *disk = disk_ptr;

    /*
     * NOTE: Files are shared with the files vector so we don't use a
     * free function to clean them up here; we
     * would double free otherwise.
     */
    vector_free(disk->files);
    free(disk);
}

static int add_file(struct disk *disk, struct file *file) {
    if (disk->free - file->size < 0) {
        return 0;
    }

    vector_add(disk->files, file);
    disk->free -= file->size;

    return 1;
}

static void print_separator(int length) {
    while (length-- > 0) {
        putchar('-');
    }

    putchar('\n');
}

/*
 * Pretty print a disk and it's contents.
 */
static void disk_print(struct disk *disk) {
    char buf[BUFSIZE];
    char *size_string = NULL;
    size_t file_index;

    /* print a nice header */
    size_string = number_to_string(disk->free);
    sprintf(buf, "Disk #%lu, %d%% (%s) free:",
            (unsigned long) disk->id,
            (int) (disk->free * 100 / cfg.disk_size),
            size_string);
    free(size_string);

    print_separator(strlen(buf));
    printf("%s\n", buf);
    print_separator(strlen(buf));

    /* and the contents */
    for (file_index = 0; file_index < disk->files->size; ++file_index) {
        struct file *file = disk->files->items[file_index];

        size_string = number_to_string(file->size);
        printf("%10s %s\n", size_string, file->name);
        free(size_string);
    }

    putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void disk_link(struct disk *disk, char *destination_directory) {
    char *path = NULL;
    char *dirty_path = NULL;
    size_t file_index;

    if (disk->id > 9999) {
        errx(1, "Number too big for format string.");
    }

    dirty_path = xmalloc(strlen(destination_directory) + 6);
    sprintf(dirty_path,
            "%s/%04lu",
            destination_directory,
            (unsigned long) disk->id);
    path = clean_path(dirty_path);
    free(dirty_path);

    for (file_index = 0; file_index < disk->files->size; ++file_index) {
        struct file *file = disk->files->items[file_index];
        char *destination_file = xmalloc(strlen(path)
                                         + strlen(file->name)
                                         + 2);
        char *slash_position = NULL;

        sprintf(destination_file, "%s/%s", path, file->name);
        slash_position = strrchr(destination_file, '/');
        *slash_position = '\0';
        make_dirs(destination_file);
        *slash_position = '/';

        if (link(file->name, destination_file) == -1) {
            err(1, "Can't link '%s' to '%s'.", file->name, destination_file);
        }

        printf("%s -> %s\n", file->name, path);
        free(destination_file);
    }

    free(path);
}

static int by_file_size_descending(const void *file_a, const void *file_b) {

    struct file *a = *((struct file **) file_a);
    struct file *b = *((struct file **) file_b);

    return b->size - a->size;
}

/*
 * Fits files onto disks following a simple algorithm; first sort files
 * by size descending, then loop over the available disks for a fit. If
 * none can hold the file create a new disk containing it.  This will
 * rapidly fill disks while the smaller remaining files will usually
 * make a good final fit.
 */
static void fit(struct vector *files, struct vector *disks) {
    size_t file_index;

    qsort(files->items, files->size, sizeof(files->items[0]),
          by_file_size_descending);

    for (file_index = 0; file_index < files->size; ++file_index) {
        struct file *file = files->items[file_index];
        size_t disk_index;
        int added = FALSE;

        for (disk_index = 0; disk_index < disks->size; ++disk_index) {
            struct disk *disk = disks->items[disk_index];

            if (add_file(disk, file)) {
                added = TRUE;
                break;
            }
        }

        if (!added) {
            struct disk *disk = disk_new(cfg.disk_size);

            if (!add_file(disk, file)) {
                errx(1, "add_file failed.");
            }

            vector_add(disks, disk);
        }
    }
}

int collect(const char *filename, const struct stat *st, int filetype,
            struct FTW *ftwbuf) {

    struct file *file = NULL;

    /* skip subdirectories if not doing a recursive collect */
    if (!cfg.do_recursive_collect && ftwbuf->level > 1) {
        return 0;
    }

    /* there might be access errors */
    if (filetype == FTW_NS || filetype == FTW_SLN || filetype == FTW_DNR) {
        err(1, "Can't access '%s'.", filename);
    }

    /* skip directories */
    if (filetype == FTW_D) {
        return 0;
    }

    /* we can only handle regular files */
    if (filetype != FTW_F) {
        err(1, "'%s' is not a regular file.", filename);
    }

    /* which are not too big to fit */
    if (st->st_size > cfg.disk_size) {
        errx(1, "Can never fit '%s' (%s).",
             filename,
             number_to_string(st->st_size));
    }

    file = file_new(filename, st->st_size);
    vector_add(cfg.files, file);

    return 0;
}

static void usage(void) {
    fprintf(stderr, "%s", usage_string);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    char *destination_directory = NULL;
    struct vector *disks = NULL;
    size_t disk_index;
    int argument_index;
    int option;

    while ((option = getopt(argc, argv, "l:nrs:")) != -1) {
        switch (option) {
            case 'l':
                destination_directory = clean_path(optarg);
                cfg.do_link_disks = TRUE;
                break;

            case 'n':
                cfg.do_show_disk_count = TRUE;
                break;

            case 'r':
                cfg.do_recursive_collect = TRUE;
                break;

            case 's':
                cfg.disk_size = string_to_number(optarg);
                break;
        }
    }

    /* A path argument and the size option is mandatory. */
    if (optind >= argc || cfg.disk_size <= 0) {
        usage();
    }

    cfg.files = vector_new();

    for (argument_index = optind; argument_index < argc; ++argument_index) {
        nftw(argv[argument_index], collect, MAXFD, 0);
    }

    if (cfg.files->size == 0) {
        errx(1, "no files found.");
    }

    disks = vector_new();
    fit(cfg.files, disks);

    /*
     * Be realistic about the number of disks to support, the helper
     * functions above assume a format string which will fit 4 digits.
     */
    if (disks->size > 9999) {
        errx(1, "Fitting takes too many (%lu) disks.", disks->size);
    }

    if (cfg.do_show_disk_count) {
        printf("%lu disk%s.\n",
               (unsigned long) disks->size,
               disks->size > 1 ? "s" : "");
        exit(EXIT_SUCCESS);
    }

    for (disk_index = 0; disk_index < disks->size; ++disk_index) {
        struct disk *disk = disks->items[disk_index];

        if (cfg.do_link_disks) {
            disk_link(disk, destination_directory);
        } else {
            disk_print(disk);
        }
    }

    vector_for_each(cfg.files, file_free);
    vector_for_each(disks, disk_free);
    vector_free(cfg.files);
    vector_free(disks);

    if (cfg.do_link_disks) {
        free(destination_directory);
    }

    return EXIT_SUCCESS;
}

