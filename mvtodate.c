/*
 * Rename given files into a folder of their date:
 * $ ls -l mvtodate.c
 * -rw-r--r-- 1 axel axel 1306 Jan 31 00:57 mvtodate.c
 * $ mvtodate mvtodate.c
 * (will move mvtodate.c to 202501/mvtodate.c)
 */

#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static char errstr[1024];

int
mvtodate(const char *src, const char *fmt)
{
	char dst[PATH_MAX], dir[PATH_MAX], *p;
	struct stat sb;

	if (lstat(src, &sb) == -1) {
		snprintf(errstr, sizeof(errstr),
		    "lstat(%s): %s.", src, strerror(errno));
		return -1;
	}

	if (!S_ISREG(sb.st_mode)) {
		snprintf(errstr, sizeof(errstr),
		    "%s is not a regular file.", src);
		return -1;
	}

	if (strftime(dir, sizeof(dir), fmt, localtime(&sb.st_mtime)) == 0) {
		snprintf(errstr, sizeof(errstr), "strftime: bad format: %s",
		    fmt);
		return -1;
	}
	p = strrchr(src, '/');
	if (p && p[1] != '\0')
		++p;
	snprintf(dst, sizeof(dst), "%s/%s", dir, p ? p : src);

	if (mkdir(dir, 0700) == -1) {
		if (errno != EEXIST) {
			snprintf(errstr, sizeof(errstr),
			    "mkdir(%s): %s.", dir, strerror(errno));
			return -1;
		}
	}

	if (rename(src, dst) == -1) {
		snprintf(errstr, sizeof(errstr),
		    "rename(%s, %s): %s.", src, dst, strerror(errno));
		return -1;
	}

	return 0;
}

void
usage(void)
{
	errx(1, "usage: mvtodate [-f fmt] file [file ...]");
}

int
main(int argc, char **argv)
{
	char *fmt = "%Y%m";
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

	if (optind >= argc)
		usage();

	for (i = optind; i < argc; ++i) {
		if (mvtodate(argv[i], fmt) == -1)
			errx(1, errstr);
	}

	return 0;
}
