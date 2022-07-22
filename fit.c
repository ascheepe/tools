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

static struct program_context {
	off_t disk_size;
	struct vector *files;
	int do_link_disks;
	int do_show_disk_count;
	int do_recursive_collect;
} ctx;

struct file {
	off_t size;
	char *name;
};

static struct file *
file_new(const char *name, off_t size)
{
	struct file *file;

	file = xmalloc(sizeof(*file));
	file->name = xstrdup(name);
	file->size = size;

	return file;
}

static void
file_free(void *pfile)
{
	struct file *file = pfile;

	free(file->name);
	free(file);
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
disk_free(void *pdisk)
{
	struct disk *disk = pdisk;

	/*
	 * NOTE: Files are shared with the files vector so we don't use a
	 * free function to clean them up here; we
	 * would double free otherwise.
	 */
	vector_free(disk->files);
	free(disk);
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
	    (unsigned long) disk->id,
	    (int) (disk->free * 100 / ctx.disk_size), sizestr);
	free(sizestr);

	hline(strlen(header));
	printf("%s\n", header);
	hline(strlen(header));

	/* and the contents */
	for (i = 0; i < disk->files->size; ++i) {
		struct file *file = disk->files->items[i];

		sizestr = number_to_string(file->size);
		printf("%10s %s\n", sizestr, file->name);
		free(sizestr);
	}

	putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void
disk_link(struct disk *disk, char *destdir)
{
	char *tmp, *path;
	size_t i, pathlen;

	if (disk->id > 9999)
		errx(1, "Number too big for format string.");

	tmp = xmalloc(strlen(destdir) + 6);
	sprintf(tmp, "%s/%04lu", destdir, (unsigned long) disk->id);
	path = clean_path(tmp);
	free(tmp);
	pathlen = strlen(path);

	for (i = 0; i < disk->files->size; ++i) {
		struct file *file = disk->files->items[i];
		char *slashpos, *destfile;

		destfile = xmalloc(pathlen + strlen(file->name) + 2);
		sprintf(destfile, "%s/%s", path, file->name);
		slashpos = destfile + pathlen;
		*slashpos = '\0';
		makedirs(destfile);
		*slashpos = '/';

		if (link(file->name, destfile) == -1)
			err(1, "Can't link '%s' to '%s'", file->name,
			    destfile);

		printf("%s -> %s\n", file->name, path);
		free(destfile);
	}

	free(path);
}

static int
byrevsize(const void *file_a, const void *file_b)
{
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
static void
fit(struct vector *files, struct vector *disks)
{
	size_t i;

	qsort(files->items, files->size, sizeof(files->items[0]), byrevsize);

	for (i = 0; i < files->size; ++i) {
		struct file *file = files->items[i];
		int added;
		size_t j;

		added = FALSE;
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
				errx(1, "add_file failed.");

			vector_add(disks, disk);
		}
	}
}

int
collect(const char *filename, const struct stat *st, int filetype,
    struct FTW *ftwbuf)
{
	struct file *file;

	/* skip subdirectories if not doing a recursive collect */
	if (!ctx.do_recursive_collect && ftwbuf->level > 1)
		return 0;

	/* there might be access errors */
	if (filetype == FTW_NS || filetype == FTW_SLN || filetype == FTW_DNR)
		errx(1, "Can't access '%s'.", filename);

	/* skip directories */
	if (filetype == FTW_D)
		return 0;

	/* we can only handle regular files */
	if (filetype != FTW_F)
		errx(1, "'%s' is not a regular file.", filename);

	/* which are not too big to fit */
	if (st->st_size > ctx.disk_size) {
		errx(1, "Can never fit '%s' (%s).",
		    filename, number_to_string(st->st_size));
	}

	file = file_new(filename, st->st_size);
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
	if (optind >= argc || ctx.disk_size <= 0)
		usage();

	ctx.files = vector_new();

	for (i = optind; (int) i < argc; ++i) {
		if (nftw(argv[i], collect, MAXFD, 0) == -1)
			err(1, "nftw");
	}

	if (ctx.files->size == 0)
		errx(1, "no files found.");

	disks = vector_new();
	fit(ctx.files, disks);

	/*
	 * Be realistic about the number of disks to support, the helper
	 * functions above assume a format string which will fit 4 digits.
	 */
	if (disks->size > 9999)
		errx(1, "Fitting takes too many (%lu) disks.", disks->size);

	if (ctx.do_show_disk_count) {
		printf("%lu disk%s.\n",
		    (unsigned long) disks->size, disks->size > 1 ? "s" : "");
		exit(EXIT_SUCCESS);
	}

	for (i = 0; i < disks->size; ++i) {
		struct disk *disk = disks->items[i];

		if (ctx.do_link_disks)
			disk_link(disk, destdir);
		else
			disk_print(disk);
	}

	vector_foreach(ctx.files, file_free);
	vector_foreach(disks, disk_free);
	vector_free(ctx.files);
	vector_free(disks);

	if (ctx.do_link_disks)
		free(destdir);

	return EXIT_SUCCESS;
}

