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
usage:  fit -s size [-l destination_directory] [-nr] path [path ...]\n\
\n\
options:\n\
  -s size     disk size in k, m, g, or t.\n\
  -l destdir  directory to link files into,\n\
              if omitted just print the disks.\n\
  -n          show the number of disks it takes.\n\
  path        path to the files to fit.\n\
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

static struct program_context
{
    off_t          disk_size;
    struct vector *files;
    int            do_link_disks;
    int            do_show_disk_count;
    int            do_recursive_collect;
}
context;

/*
 * To be able to fit files and present a disklist file stores the
 * size and the name of a file.
 */
struct file_info
{
    off_t  size;
    char  *name;
};

static struct file_info *file_info_new(const char *name, off_t size)
{
    struct file_info *file_info;

    file_info       = xmalloc(sizeof(*file_info));
    file_info->name = xstrdup(name);
    file_info->size = size;

    return file_info;
}

static void file_info_free(void *file_info_ptr)
{
    struct file_info *file_info = file_info_ptr;

    free(file_info->name);
    free(file_info);
}

/*
 * A disk with 'free' free space contains a vector of files. It's id
 * is an incrementing number which doubles as the total number of
 * disks made.
 */
struct disk
{
    struct vector *files;
    off_t          free;
    size_t         id;
};

static struct disk *disk_new(off_t size)
{
    static size_t  id;
    struct disk   *disk;

    disk        = xmalloc(sizeof(*disk));
    disk->files = vector_new();
    disk->free  = size;
    disk->id    = ++id;

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
    free(disk);
}

static int disk_add_file(struct disk *disk, struct file_info *file_info)
{
    if (disk->free - file_info->size < 0)
    {
        return 0;
    }

    vector_add(disk->files, file_info);
    disk->free -= file_info->size;

    return 1;
}

static void print_separator(int length)
{
    while (length-- > 0)
    {
        putchar('-');
    }

    putchar('\n');
}

/*
 * Pretty print a disk and it's contents.
 */
static void disk_print(struct disk *disk)
{
    char    buffer[BUFSIZE];
    char   *size_string = NULL;
    size_t  file_info_nr;

    /* print a nice header */
    size_string = number_to_string(disk->free);
    sprintf(buffer, "Disk #%lu, %d%% (%s) free:", (unsigned long) disk->id,
            (int) (disk->free * 100 / context.disk_size), size_string);
    free(size_string);

    print_separator(strlen(buffer));
    printf("%s\n", buffer);
    print_separator(strlen(buffer));

    /* and the contents */
    for (file_info_nr = 0; file_info_nr < disk->files->size; ++file_info_nr)
    {
        struct file_info *file_info = disk->files->items[file_info_nr];

        size_string = number_to_string(file_info->size);
        printf("%10s %s\n", size_string, file_info->name);
        free(size_string);
    }

    putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void disk_link(struct disk *disk, char *destination_directory)
{
    char   *dirty_path   = NULL;
    char   *path         = NULL;
    size_t  file_info_nr;

    if (disk->id > 9999)
    {
        errx(1, "Number too big for format string.");
    }

    dirty_path = xmalloc(strlen(destination_directory) + 6);
    sprintf(dirty_path, "%s/%04lu", destination_directory,
            (unsigned long) disk->id);
    path = clean_path(dirty_path);
    free(dirty_path);

    for (file_info_nr = 0; file_info_nr < disk->files->size; ++file_info_nr)
    {
        struct file_info *file_info        = disk->files->items[file_info_nr];
        char             *destination_file = NULL;
        char             *slash_position   = NULL;

        destination_file = xmalloc(strlen(path) + strlen(file_info->name) + 2);
        sprintf(destination_file, "%s/%s", path, file_info->name);
        slash_position = strrchr(destination_file, '/');
        *slash_position = '\0';
        make_dirs(destination_file);
        *slash_position = '/';

        if (link(file_info->name, destination_file) == -1)
        {
            err(1, "Can't link '%s' to '%s'.", file_info->name, destination_file);
        }

        printf("%s -> %s\n", file_info->name, path);
        free(destination_file);
    }

    free(path);
}

static int by_file_size_descending(const void *file_info_a,
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
    size_t file_info_nr;

    qsort(files->items, files->size, sizeof(files->items[0]),
          by_file_size_descending);

    for (file_info_nr = 0; file_info_nr < files->size; ++file_info_nr)
    {
        struct file_info *file    = files->items[file_info_nr];
        int               added   = FALSE;
        size_t            disk_nr;

        for (disk_nr = 0; disk_nr < disks->size; ++disk_nr)
        {
            struct disk *disk = disks->items[disk_nr];

            if (disk_add_file(disk, file))
            {
                added = TRUE;
                break;
            }
        }

        if (!added)
        {
            struct disk *disk;

            disk = disk_new(context.disk_size);

            if (!disk_add_file(disk, file))
            {
                errx(1, "disk_add_file failed.");
            }

            vector_add(disks, disk);
        }
    }
}

int collect(const char *filename, const struct stat *st, int filetype,
            struct FTW *ftw_buffer)
{

    struct file_info *file_info = NULL;

    /* skip subdirectories if not doing a recursive collect */
    if (!context.do_recursive_collect && ftw_buffer->level > 1)
    {
        return 0;
    }

    /* there might be access errors */
    if (filetype == FTW_NS || filetype == FTW_SLN || filetype == FTW_DNR)
    {
        err(1, "Can't access '%s'.", filename);
    }

    /* skip directories */
    if (filetype == FTW_D)
    {
        return 0;
    }

    /* we can only handle regular files */
    if (filetype != FTW_F)
    {
        err(1, "'%s' is not a regular file.", filename);
    }

    /* which are not too big to fit */
    if (st->st_size > context.disk_size)
    {
        errx(1, "Can never fit '%s' (%s).", filename,
             number_to_string(st->st_size));
    }

    file_info = file_info_new(filename, st->st_size);
    vector_add(context.files, file_info);

    return 0;
}

static void usage(void)
{
    fprintf(stderr, "%s", usage_string);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    char          *destination_directory = NULL;
    struct vector *disks                 = NULL;
    int            argument_nr;
    int            option;
    size_t         disk_nr;

    while ((option = getopt(argc, argv, "l:nrs:")) != -1)
    {
        switch (option)
        {
            case 'l':
                destination_directory = clean_path(optarg);
                context.do_link_disks = TRUE;
                break;

            case 'n':
                context.do_show_disk_count = TRUE;
                break;

            case 'r':
                context.do_recursive_collect = TRUE;
                break;

            case 's':
                context.disk_size = string_to_number(optarg);
                break;
        }
    }

    /* A path argument and the size option is mandatory. */
    if (optind >= argc || context.disk_size <= 0)
    {
        usage();
    }

    context.files = vector_new();

    for (argument_nr = optind; argument_nr < argc; ++argument_nr)
    {
        nftw(argv[argument_nr], collect, MAXFD, 0);
    }

    if (context.files->size == 0)
    {
        errx(1, "no files found.");
    }

    disks = vector_new();
    fit(context.files, disks);

    /*
     * Be realistic about the number of disks to support, the helper
     * functions above assume a format string which will fit 4 digits.
     */
    if (disks->size > 9999)
    {
        errx(1, "Fitting takes too many (%lu) disks.", disks->size);
    }

    if (context.do_show_disk_count)
    {
        printf("%lu disk%s.\n", (unsigned long) disks->size,
               disks->size > 1 ? "s" : "");
        exit(EXIT_SUCCESS);
    }

    for (disk_nr = 0; disk_nr < disks->size; ++disk_nr)
    {
        struct disk *disk = disks->items[disk_nr];

        if (context.do_link_disks)
        {
            disk_link(disk, destination_directory);
        }
        else
        {
            disk_print(disk);
        }
    }

    vector_for_each(context.files, file_info_free);
    vector_for_each(disks, disk_free);
    vector_free(context.files);
    vector_free(disks);

    if (context.do_link_disks)
    {
        free(destination_directory);
    }

    return EXIT_SUCCESS;
}

