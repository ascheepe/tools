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
  -s size        Disk size in k, m, g, or t.\n\
  -l destination Directory to link files into,\n\
                 if omitted just print the disks.\n\
  -n             Just show the number of disks it takes.\n\
  -r             Do a recursive search.\n\
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

static struct configuration {
	off_t disk_size;
	struct vector *files;
	int link;
	int print;
	int recurse;
} cfg;

struct afile {
	off_t size;
	char *name;
};

static struct afile *
afile_new(const char *name, off_t size)
{
	struct afile *afile;

	afile = xmalloc(sizeof(*afile));
	afile->name = xstrdup(name);
	afile->size = size;

	return afile;
}

static void
afile_free(void *afilep)
{
	struct afile *afile = afilep;

	xfree(afile->name);
	xfree(afile);
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
disk_free(void *diskp)
{
	struct disk *disk = diskp;

	vector_foreach(disk->files, afile_free);
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
	    (ulong)disk->id,
	    (int)(disk->free * 100 / cfg.disk_size), sizestr);
	xfree(sizestr);

	hline(strlen(header));
	printf("%s\n", header);
	hline(strlen(header));

	/* and the contents */
	for (i = 0; i < disk->files->size; ++i) {
		struct afile *afile = disk->files->items[i];

		sizestr = number_to_string(afile->size);
		printf("%10s %s\n", sizestr, afile->name);
		xfree(sizestr);
	}

	putchar('\n');
}

/*
 * Link the contents of a disk to the given destination directory.
 */
static void
disk_link(struct disk *disk, char *dstdir)
{
	size_t i, len;

	len = strlen(dstdir);
	for (i = 0; i < disk->files->size; ++i) {
		struct afile *afile = disk->files->items[i];
		char *dst;

		dst = xmalloc(len + strlen(afile->name) + 2);
		sprintf(dst, "%s/%s", dstdir, afile->name);
		xlink(afile->name, dst);
		printf("%s -> %s\n", afile->name, dstdir);
		xfree(dst);
	}
}

static int
by_size_descending(const void *afile_a, const void *afile_b)
{
	struct afile *a = *((struct afile **)afile_a);
	struct afile *b = *((struct afile **)afile_b);

	return b->size - a->size;
}

static int
add_file(struct disk *disk, struct afile *afile)
{
	if (disk->free - afile->size < 0)
		return FALSE;

	vector_add(disk->files, afile);
	disk->free -= afile->size;

	return TRUE;
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
		struct afile *afile = files->items[i];
		int added = FALSE;
		size_t j;

		for (j = 0; j < disks->size; ++j) {
			struct disk *disk = disks->items[j];

			if (add_file(disk, afile)) {
				added = TRUE;
				break;
			}
		}

		if (!added) {
			struct disk *disk;

			disk = disk_new(cfg.disk_size);
			if (!add_file(disk, afile))
				die("add_file failed.");

			vector_add(disks, disk);
		}
	}
}

static int
collect(const char *filename, const struct stat *st,
    int filetype, struct FTW *ftwbuf)
{
	struct afile *afile;

	/* skip subdirectories if not doing a recursive collect */
	if (!cfg.recurse && ftwbuf->level > 1)
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

	afile = afile_new(filename, st->st_size);
	vector_add(cfg.files, afile);

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
	int opt;

	while ((opt = getopt(argc, argv, "l:nrs:")) != -1) {
		switch (opt) {
		case 'l':
			basedir = clean_path(optarg);
			cfg.link = TRUE;
			break;
		case 'n':
			cfg.print = TRUE;
			break;
		case 'r':
			cfg.recurse = TRUE;
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

	/* There is room for 4 digits in the format string(s). */
	if (disks->size > 9999)
		die("Fitting takes too many (%lu) disks.", disks->size);

	if (cfg.print) {
		char *plural = disks->size < 1 ? "disk" : "disks";

		printf("%lu %s.\n", (ulong)disks->size, plural);
		exit(EXIT_SUCCESS);
	}

	for (i = 0; i < disks->size; ++i) {
		struct disk *disk = disks->items[i];

		if (cfg.link) {
			char *dstdir;

			dstdir = xmalloc(strlen(basedir) + 6);
			sprintf(dstdir, "%s/%04lu", basedir, (ulong)disk->id);
			makedirs(dstdir);
			disk_link(disk, dstdir);
			xfree(dstdir);
		} else
			disk_print(disk);
	}

	vector_foreach(disks, disk_free);
	vector_free(cfg.files);
	vector_free(disks);

	if (cfg.link)
		xfree(basedir);

	return EXIT_SUCCESS;
}
