#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static char errstr[1024];

int
mvd(char *file, char *destdir, char *fmt)
{
	char target[PATH_MAX], datestr[PATH_MAX];
	struct stat sb;

	if (stat(file, &sb) == -1) {
		snprintf(errstr, sizeof(errstr),
		    "%s: %s.", file, strerror(errno));
		return -1;
	}

	if (strftime(datestr, sizeof(datestr),
	    fmt, localtime(&sb.st_mtime)) == 0) {
		snprintf(errstr, sizeof(errstr), "bad format: %s", fmt);
		return -1;
	}

	snprintf(target, sizeof(target), "%s/%s", destdir, datestr);
	if (mkdir(target, 0700) == -1) {
		if (errno != EEXIST) {
			snprintf(errstr, sizeof(errstr),
			    "mkdir %s: %s.", target, strerror(errno));
			return -1;
		}
	}

	snprintf(target, sizeof(target),
	    "%s/%s/%s", destdir, datestr, basename(file));
	if (rename(file, target) == -1) {
		snprintf(errstr, sizeof(errstr),
		    "rename %s to %s: %s.", file, target, strerror(errno));
		return -1;
	}

	return 0;
}

void
usage(void)
{
	errx(1, "usage: mvd [-f fmt] file [file ...] target");
}

int
isdir(const char *name)
{
	struct stat sb;

	if (stat(name, &sb) == -1)
		errx(1, "%s: %s", name, strerror(errno));

	if (S_ISDIR(sb.st_mode))
		return 1;

	return 0;
}

int
main(int argc, char **argv)
{
	char *destdir = argv[argc - 1], *fmt = "%Y%m";
	int i, opt;

	while ((opt = getopt(argc, argv, "f:")) != -1) {
		switch (opt) {
		case 'f':
			fmt = optarg;
			break;
		default:
			usage();
		}
	}

	if (optind >= argc || !isdir(destdir))
		usage();

	for (i = optind; i < argc - 1; ++i) {
		if (mvd(argv[i], destdir, fmt) == -1)
			errx(1, errstr);
	}

	return 0;
}
