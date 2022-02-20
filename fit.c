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

#include "vector.h"
#include "utils.h"

static struct  {
	off_t disk_size;
	struct vector *files;
	int lflag;
	int nflag;
	int rflag;
} ctx;

/*
 * To be able to fit files and present a disklist disk_entry stores the
 * size and the name of a file.
 */
struct disk_entry {
	off_t size;
	char *name;
};

static struct disk_entry *
disk_entry_new(const char *name, off_t size)
{
	struct disk_entry *e;

	e = xmalloc(sizeof(*e));
	e->name = xstrdup(name);
	e->size = size;

	return e;
}

static void
disk_entry_free(void *pdisk_entry)
{
	struct disk_entry *e = pdisk_entry;

	free(e->name);
	free(e);
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

static struct disk *
disk_new(off_t size)
{
	static size_t disk_id;
	struct disk *d;

	d = xmalloc(sizeof(*d));
	d->files = vector_new();
	d->free = size;
	d->id = ++disk_id;

	return d;
}

static void
disk_free(void *pdisk)
{
	struct disk *d = pdisk;

	/*
	 * NOTE: Files are shared with the files vector so we don't use a
	 * free function to clean them up here; we
	 * would double free otherwise.
	 */
	vector_free(d->files);
	free(d);
}

static int
disk_add(struct disk *d, struct disk_entry *e)
{
	if (d->free - e->size < 0)
		return 0;

	vector_add(d->files, e);
	d->free -= e->size;

	return 1;
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
disk_print(struct disk *d)
{
	char buf[BUFSIZE];
	char *sizestr;
	size_t i;

	/* print a nice header */
	sizestr = number_to_string(d->free);
	sprintf(buf, "Disk #%lu, %d%% (%s) free:", (unsigned long) d->id,
	    (int) (d->free * 100 / ctx.disk_size), sizestr);
	free(sizestr);

	hline(strlen(buf));
	printf("%s\n", buf);
	hline(strlen(buf));

	/* and the contents */
	for (i = 0; i < d->files->size; ++i) {
		struct disk_entry *e = d->files->items[i];

		sizestr = number_to_string(e->size);
		printf("%10s %s\n", sizestr, e->name);
		free(sizestr);
	}

	putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void
disk_link(struct disk *d, char *destdir)
{
	char *tmp, *path;
	size_t i;

	if (d->id > 9999)
		errx(1, "Number too big for format string.");

	tmp = xmalloc(strlen(destdir) + 6);
	sprintf(tmp, "%s/%04lu", destdir, (unsigned long) d->id);
	path = clean_path(tmp);
	free(tmp);

	for (i = 0; i < d->files->size; ++i) {
		struct disk_entry *e = d->files->items[i];
		char *slashpos, *destfile;

		destfile = xmalloc(strlen(path) + strlen(e->name) + 2);
		sprintf(destfile, "%s/%s", path, e->name);
		slashpos = strrchr(destfile, '/');
		*slashpos = '\0';
		make_dirs(destfile);
		*slashpos = '/';

		if (link(e->name, destfile) == -1)
			err(1, "Can't link '%s' to '%s'.", e->name, destfile);

		printf("%s -> %s\n", e->name, path);
		free(destfile);
	}

	free(path);
}

static int
by_size_descending(const void *disk_entry_a, const void *disk_entry_b)
{
	struct disk_entry *a = *((struct disk_entry **) disk_entry_a);
	struct disk_entry *b = *((struct disk_entry **) disk_entry_b);

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
static void
fit_files(struct vector *files, struct vector *disks)
{
	size_t i;

	qsort(files->items, files->size, sizeof(files->items[0]),
	    by_size_descending);

	for (i = 0; i < files->size; ++i) {
		struct disk_entry *e = files->items[i];
		int added = false;
		size_t j;

		for (j = 0; j < disks->size; ++j) {
			struct disk *d = disks->items[j];

			if (disk_add(d, e)) {
				added = true;
				break;
			}
		}

		if (!added) {
			struct disk *d;

			d = disk_new(ctx.disk_size);
			if (!disk_add(d, e))
				errx(1,
				    "Unexpected error while adding file to disk.");

			vector_add(disks, d);
		}
	}
}

int
collect_files(const char *filename, const struct stat *st, int file_type,
    struct FTW *ftwbuf)
{

	/* there might be access errors */
	if (file_type == FTW_NS || file_type == FTW_SLN ||
	    file_type == FTW_DNR)
		err(1, "Can't access '%s'.", filename);

	/* skip directories */
	if (file_type == FTW_D)
		return 0;

	/* skip subdirectories if not doing a recursive collect */
	if (!ctx.rflag && ftwbuf->level > 1)
		return 0;

	/* collect regular files which can fit on a disk */
	if (file_type == FTW_F) {
		struct disk_entry *e;

		if (st->st_size > ctx.disk_size) {
			errx(1, "Can never fit '%s' (%s).", filename,
			    number_to_string(st->st_size));
		}

		e = disk_entry_new(filename, st->st_size);
		vector_add(ctx.files, e);
	} else
		err(1, "'%s' is not a regular file.", filename);

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
	if (optind >= argc || ctx.disk_size <= 0)
		usage();

	ctx.files = vector_new();

	for (arg = optind; arg < argc; ++arg)
		nftw(argv[arg], collect_files, MAXFD, 0);

	if (ctx.files->size == 0)
		errx(1, "no files found.");

	disks = vector_new();
	fit_files(ctx.files, disks);

	/*
	 * Be realistic about the number of disks to support, the helper
	 * functions above assume a format string which will fit 4 digits.
	 */
	if (disks->size > 9999)
		errx(1, "Fitting takes too many (%lu) disks.", disks->size);

	if (ctx.nflag) {
		printf("%lu disk%s.\n", (unsigned long) disks->size,
		    disks->size > 1 ? "s" : "");
		exit(EXIT_SUCCESS);
	}

	for (i = 0; i < disks->size; ++i) {
		struct disk *d = disks->items[i];

		if (ctx.lflag)
			disk_link(d, destdir);
		else
			disk_print(d);
	}

	vector_foreach(ctx.files, disk_entry_free);
	vector_foreach(disks, disk_free);
	vector_free(ctx.files);
	vector_free(disks);

	if (ctx.lflag)
		free(destdir);

	return EXIT_SUCCESS;
}
