/*
 * Copyright (c) 2024 Axel Scheepers
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

static struct context {
	off_t disk_size;
	struct vector *files;
	int do_link_files;
	int do_show_only;
	int do_recursive_search;
	int verbose;
} ctx;

struct file {
	off_t size;
	char *name;
};

static struct file *
file_new(const char *name, off_t size)
{
	struct file *file;

	file = xcalloc(1, sizeof(*file));
	file->name = xstrdup(name);
	file->size = size;

	return file;
}

static void
file_free(void *file_ptr)
{
	struct file *file = file_ptr;

	xfree(file->name);
	xfree(file);
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

	disk = xcalloc(1, sizeof(*disk));
	disk->files = vector_new();
	disk->free = size;
	disk->id = ++id;

	return disk;
}

static void
disk_free(void *disk_ptr)
{
	struct disk *disk = disk_ptr;

	vector_foreach(disk->files, file_free);
	vector_free(disk->files);
	xfree(disk);
}

static void
hline(int len)
{
	while (len-- > 0)
		putchar('-');

	putchar('\n');
}

static void
print_header(struct disk *disk)
{
	char header[BUFSIZE];
	char *disk_free;
	size_t len;

	disk_free = number_to_string(disk->free);
	len = sprintf(header, "Disk #%lu, %d%% (%s) free:",
	    (ulong) disk->id,
	    (int)(disk->free * 100 / ctx.disk_size), disk_free);
	xfree(disk_free);

	hline(len);
	printf("%s\n", header);
	hline(len);
}

/*
 * Pretty print a disk and it's contents.
 */
static void
disk_print(struct disk *disk)
{
	size_t i;

	print_header(disk);
	for (i = 0; i < disk->files->size; ++i) {
		struct file *file = disk->files->items[i];
		char *file_size;

		file_size = number_to_string(file->size);
		printf("%10s %s\n", file_size, file->name);
		xfree(file_size);
	}

	putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void
disk_link(struct disk *disk, char *destdir)
{
	size_t i, len;

	len = strlen(destdir);
	for (i = 0; i < disk->files->size; ++i) {
		struct file *file = disk->files->items[i];
		char *linkdest;

		linkdest = xcalloc(1, len + strlen(file->name) + 2);
		sprintf(linkdest, "%s/%s", destdir, file->name);
		xlink(file->name, linkdest);
		if (ctx.verbose)
			printf("%s -> %s\n", file->name, destdir);
		xfree(linkdest);
	}
}

static int
add_file(struct disk *disk, struct file *file)
{
	if (disk->free - file->size < 0)
		return FALSE;

	vector_add(disk->files, file);
	disk->free -= file->size;

	return TRUE;
}

static int
by_size_descending(const void *file_a, const void *file_b)
{
	struct file *a = *((struct file **)file_a);
	struct file *b = *((struct file **)file_b);

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
		struct file *file = files->items[i];
		int added = FALSE;
		size_t j;

		for (j = 0; j < disks->size; ++j) {
			struct disk *disk = disks->items[j];

			if (add_file(disk, file)) {
				added = TRUE;
				break;
			}
		}

		if (!added) {
			struct disk *disk;

			disk = disk_new(ctx.disk_size);
			if (!add_file(disk, file))
				die("add_file failed.");

			vector_add(disks, disk);
		}
	}
}

static int
collect_files(const char *fpath, const struct stat *st, int type,
    struct FTW *ftwbuf)
{
	struct file *file;

	/* skip subdirectories if not doing a recursive collect_files */
	if (!ctx.do_recursive_search && ftwbuf->level > 1)
		return 0;

	/* there might be access errors */
	if (type == FTW_NS || type == FTW_SLN || type == FTW_DNR)
		die("Can't access '%s':", fpath);

	/* skip directories */
	if (type == FTW_D)
		return 0;

	/* we can only handle regular files */
	if (type != FTW_F)
		die("'%s' is not a regular file.", fpath);

	/* which are not too big to fit */
	if (st->st_size > ctx.disk_size)
		die("Can never fit '%s' (%s).", fpath,
		    number_to_string(st->st_size));

	file = file_new(fpath, st->st_size);
	vector_add(ctx.files, file);

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
	char *basedir = NULL;
	struct vector *disks = NULL;
	size_t i;
	int option;

	while ((option = getopt(argc, argv, "l:nrs:v")) != -1) {
		switch (option) {
		case 'l':
			basedir = clean_path(optarg);
			ctx.do_link_files = 1;
			break;
		case 'n':
			ctx.do_show_only = 1;
			break;
		case 'r':
			ctx.do_recursive_search = 1;
			break;
		case 's':
			ctx.disk_size = string_to_number(optarg);
			break;
		case 'v':
			ctx.verbose = 1;
			break;
		}
	}

	/* A path argument and the size option is mandatory. */
	if (optind >= argc || ctx.disk_size <= 0)
		usage();

	ctx.files = vector_new();
	for (i = optind; (int)i < argc; ++i)
		if (nftw(argv[i], collect_files, MAXFD, 0) == -1)
			die("nftw:");

	if (ctx.files->size == 0)
		die("no files found.");

	disks = vector_new();
	fit(ctx.files, disks);

	/* There is room for 4 digits in the format string(s). */
	if (disks->size > 9999)
		die("Fitting takes too many (%lu) disks.", disks->size);

	if (ctx.do_show_only) {
		printf("%lu %s.\n", (ulong) disks->size,
		    disks->size == 1 ? "disk" : "disks");
		exit(EXIT_SUCCESS);
	}

	for (i = 0; i < disks->size; ++i) {
		struct disk *disk = disks->items[i];

		if (ctx.do_link_files) {
			char *dest;

			dest = xcalloc(1, strlen(basedir) + 6);
			sprintf(dest, "%s/%04lu", basedir, (ulong) disk->id);
			make_directories(dest);
			disk_link(disk, dest);
			xfree(dest);
		} else
			disk_print(disk);
	}

	vector_foreach(disks, disk_free);
	vector_free(ctx.files);
	vector_free(disks);

	if (ctx.do_link_files)
		xfree(basedir);

	return EXIT_SUCCESS;
}
