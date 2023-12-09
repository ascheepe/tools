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

static struct configuration {
	off_t disk_size;
	struct vector *files;
	int do_link_disks;
	int do_show_disk_count;
	int do_recursive_collect;
} cfg;

struct file_info {
	off_t size;
	char *name;
};

static struct file_info *
file_info_new(const char *name, off_t size)
{
	struct file_info *file_info;

	file_info = xmalloc(sizeof(*file_info));
	file_info->name = xstrdup(name);
	file_info->size = size;

	return file_info;
}

static void
file_info_free(void *file_info_ptr)
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

static struct disk *
disk_new(off_t size)
{
	struct disk *disk;
	static size_t id;

	disk = xmalloc(sizeof(*disk));
	disk->files = vector_new();
	disk->free = size;
	disk->id = ++id;

	return disk;
}

static void
disk_free(void *disk_ptr)
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

static int
add_file(struct disk *disk, struct file_info *file_info)
{
	if (disk->free - file_info->size < 0)
		return FALSE;

	vector_add(disk->files, file_info);
	disk->free -= file_info->size;

	return TRUE;
}

static void
hline(int len)
{
	while (len-- > 0)
		putchar('-');

	putchar('\n');
}

/*
 * Pretty print a disk and it's contents.
 */
static void
disk_print(struct disk *disk)
{
	char header[BUFSIZE];
	char *sizestr;
	size_t i;

	/* print a nice header */
	sizestr = number_to_string(disk->free);
	sprintf(header, "Disk #%lu, %d%% (%s) free:",
	    (unsigned long)disk->id,
	    (int)(disk->free * 100 / cfg.disk_size), sizestr);
	xfree(sizestr);

	hline(strlen(header));
	printf("%s\n", header);
	hline(strlen(header));

	/* and the contents */
	for (i = 0; i < disk->files->size; ++i) {
		struct file_info *file_info = disk->files->items[i];

		sizestr = number_to_string(file_info->size);
		printf("%10s %s\n", sizestr, file_info->name);
		xfree(sizestr);
	}

	putchar('\n');
}

static void
xlink(const char *src, const char *dst)
{
	if (link(src, dst) == -1)
		die("Can't link '%s' to '%s':", src, dst);
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void
disk_link(struct disk *disk, char *dstdir)
{
	char *tmp;
	size_t i, len;

	if (disk->id > 9999)
		die("Number too big for format string.");

	tmp = xmalloc(strlen(dstdir) + 6);
	sprintf(tmp, "%s/%04lu", dstdir, (unsigned long)disk->id);
	dstdir = clean_path(tmp);
	xfree(tmp);
	makedirs(dstdir);
	len = strlen(dstdir);

	for (i = 0; i < disk->files->size; ++i) {
		struct file_info *file_info = disk->files->items[i];
		char *dst;

		dst = xmalloc(len + strlen(file_info->name) + 2);
		sprintf(dst, "%s/%s", dstdir, file_info->name);
		xlink(file_info->name, dst);
		printf("%s -> %s\n", file_info->name, dstdir);
		xfree(dst);
	}

	xfree(dstdir);
}

static int
by_size_descending(const void *file_info_a, const void *file_info_b)
{
	struct file_info *a = *((struct file_info **)file_info_a);
	struct file_info *b = *((struct file_info **)file_info_b);

	return b->size - a->size;
}

/*
 * Fits files onto disks following a simple algorithm; first sort files
 * by size descending, then loop over the available disks for a fit. If
 * none can hold the file create a new disk containing it.  This will
 * rapidly fill disks while the smaller remaining files will usually
 * make a good final fit.
 */
static void
fit(struct vector *files, struct vector *disks)
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

			disk = disk_new(cfg.disk_size);
			if (!add_file(disk, file_info))
				die("add_file failed.");

			vector_add(disks, disk);
		}
	}
}

static int
collect(const char *filename, const struct stat *st,
    int filetype, struct FTW *ftwbuf)
{
	struct file_info *file_info;

	/* skip subdirectories if not doing a recursive collect */
	if (!cfg.do_recursive_collect && ftwbuf->level > 1)
		return 0;

	/* there might be access errors */
	if (filetype == FTW_NS || filetype == FTW_SLN || filetype == FTW_DNR)
		die("Can't access '%s':", filename);

	/* skip directories */
	if (filetype == FTW_D)
		return 0;

	/* we can only handle regular files */
	if (filetype != FTW_F)
		die("'%s' is not a regular file.", filename);

	/* which are not too big to fit */
	if (st->st_size > cfg.disk_size)
		die("Can never fit '%s' (%s).",
		    filename, number_to_string(st->st_size));

	file_info = file_info_new(filename, st->st_size);
	vector_add(cfg.files, file_info);

	return 0;
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
	char *dstdir = NULL;
	struct vector *disks = NULL;
	size_t i;
	int opt;

	while ((opt = getopt(argc, argv, "l:nrs:")) != -1) {
		switch (opt) {
		case 'l':
			dstdir = clean_path(optarg);
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
	if (optind >= argc || cfg.disk_size <= 0)
		usage();

	cfg.files = vector_new();

	for (i = optind; (int)i < argc; ++i)
		if (nftw(argv[i], collect, MAXFD, 0) == -1)
			die("nftw:");

	if (cfg.files->size == 0)
		die("no files found.");

	disks = vector_new();
	fit(cfg.files, disks);

	/*
	 * Be realistic about the number of disks to support, the helper
	 * functions above assume a format string which will fit 4 digits.
	 */
	if (disks->size > 9999)
		die("Fitting takes too many (%lu) disks.", disks->size);

	if (cfg.do_show_disk_count) {
		printf("%lu disk%s.\n", (unsigned long)disks->size,
		    disks->size > 1 ? "s" : "");
		exit(EXIT_SUCCESS);
	}

	for (i = 0; i < disks->size; ++i) {
		struct disk *disk = disks->items[i];

		if (cfg.do_link_disks)
			disk_link(disk, dstdir);
		else
			disk_print(disk);
	}

	vector_foreach(cfg.files, file_info_free);
	vector_foreach(disks, disk_free);
	vector_free(cfg.files);
	vector_free(disks);

	if (cfg.do_link_disks)
		xfree(dstdir);

	return EXIT_SUCCESS;
}
