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
  -s size       disk size in k, m, g, or t.\n\
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

static struct {
	off_t disksize;
	struct vector *files;
	int lflag;
	int nflag;
	int rflag;
} ctx;

/*
 * To be able to fit files and present a disklist file stores the
 * size and the name of a file.
 */
struct file {
	off_t size;
	char *name;
};

static struct file *
newfile(const char *name, off_t size)
{
	struct file *file;

	file = xmalloc(sizeof(*file));
	file->name = xstrdup(name);
	file->size = size;

	return file;
}

static void
freefile(void *pfile)
{
	struct file *file = pfile;

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

static struct disk *
newdisk(off_t size)
{
	static size_t id;
	struct disk *disk;

	disk = xmalloc(sizeof(*disk));
	disk->files = vector_new();
	disk->free = size;
	disk->id = ++id;

	return disk;
}

static void
freedisk(void *pdisk)
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
addfile(struct disk *disk, struct file *file)
{
	if (disk->free - file->size < 0)
		return 0;

	vector_add(disk->files, file);
	disk->free -= file->size;

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
printdisk(struct disk *disk)
{
	char buf[BUFSIZE];
	char *sizestr;
	size_t i;

	/* print a nice header */
	sizestr = number_to_string(disk->free);
	sprintf(buf, "Disk #%lu, %d%% (%s) free:", (unsigned long) disk->id,
	    (int) (disk->free * 100 / ctx.disksize), sizestr);
	free(sizestr);

	hline(strlen(buf));
	printf("%s\n", buf);
	hline(strlen(buf));

	/* and the contents */
	for (i = 0; i < disk->files->size; ++i) {
		struct file *file;

		file = vector_nth(disk->files, i);
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
linkdisk(struct disk *disk, char *destdir)
{
	char *tmp, *path;
	size_t i;

	if (disk->id > 9999)
		errx(1, "Number too big for format string.");

	tmp = xmalloc(strlen(destdir) + 6);
	sprintf(tmp, "%s/%04lu", destdir, (unsigned long) disk->id);
	path = cleanpath(tmp);
	free(tmp);

	for (i = 0; i < disk->files->size; ++i) {
		char *slashpos, *destfile;
		struct file *file;

		file = vector_nth(disk->files, i);
		destfile = xmalloc(strlen(path) + strlen(file->name) + 2);
		sprintf(destfile, "%s/%s", path, file->name);
		slashpos = strrchr(destfile, '/');
		*slashpos = '\0';
		makedirs(destfile);
		*slashpos = '/';

		if (link(file->name, destfile) == -1)
			err(1, "Can't link '%s' to '%s'.", file->name,
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
		struct file *file;
		int added;
		size_t j;

		file = vector_nth(files, i);
		added = false;
		for (j = 0; j < disks->size; ++j) {
			struct disk *disk;

			disk = vector_nth(disks, j);
			if (addfile(disk, file)) {
				added = true;
				break;
			}
		}

		if (!added) {
			struct disk *disk;

			disk = newdisk(ctx.disksize);
			if (!addfile(disk, file))
				errx(1, "addfile failed.");

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
	if (!ctx.rflag && ftwbuf->level > 1)
		return 0;

	/* there might be access errors */
	if (filetype == FTW_NS || filetype == FTW_SLN || filetype == FTW_DNR)
		err(1, "Can't access '%s'.", filename);

	/* skip directories */
	if (filetype == FTW_D)
		return 0;

	/* we can only handle regular files */
	if (filetype != FTW_F)
		err(1, "'%s' is not a regular file.", filename);

	/* which are not too big to fit */
	if (st->st_size > ctx.disksize)
		errx(1, "Can never fit '%s' (%s).", filename,
		    number_to_string(st->st_size));

	file = newfile(filename, st->st_size);
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
	int arg, opt;
	size_t i;

	while ((opt = getopt(argc, argv, "l:nrs:")) != -1) {
		switch (opt) {
		case 'l':
			destdir = cleanpath(optarg);
			ctx.lflag = true;
			break;
		case 'n':
			ctx.nflag = true;
			break;
		case 'r':
			ctx.rflag = true;
			break;
		case 's':
			ctx.disksize = string_to_number(optarg);
			break;
		}
	}

	/* A path argument and the size option is mandatory. */
	if (optind >= argc || ctx.disksize <= 0)
		usage();

	ctx.files = vector_new();

	for (arg = optind; arg < argc; ++arg)
		nftw(argv[arg], collect, MAXFD, 0);

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

	if (ctx.nflag) {
		printf("%lu disk%s.\n", (unsigned long) disks->size,
		    disks->size > 1 ? "s" : "");
		exit(EXIT_SUCCESS);
	}

	for (i = 0; i < disks->size; ++i) {
		struct disk *d;

		d = vector_nth(disks, i);
		if (ctx.lflag)
			linkdisk(d, destdir);
		else
			printdisk(d);
	}

	vector_foreach(ctx.files, freefile);
	vector_foreach(disks, freedisk);
	vector_free(ctx.files);
	vector_free(disks);

	if (ctx.lflag)
		free(destdir);

	return EXIT_SUCCESS;
}
