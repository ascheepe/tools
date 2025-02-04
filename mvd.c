#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char errstr[1024];

int
mvd(char *file, char *fmt)
{
	char target[PATH_MAX], datestr[PATH_MAX];
	struct stat sb;

	if (stat(file, &sb) == -1) {
		snprintf(errstr, sizeof(errstr),
		    "%s: %s.", file, strerror(errno));
		return -1;
	}

	if (!S_ISREG(sb.st_mode)) {
		snprintf(errstr, sizeof(errstr),
		    "not a regular file: %s.", file);
		return -1;
	}

	if (strftime(datestr, sizeof(datestr),
	    fmt, localtime(&sb.st_mtime)) == 0) {
		snprintf(errstr, sizeof(errstr), "bad format: %s", fmt);
		return -1;
	}

	if (mkdir(datestr, 0700) == -1) {
		if (errno != EEXIST) {
			snprintf(errstr, sizeof(errstr),
			    "mkdir %s: %s.", datestr, strerror(errno));
			return -1;
		}
	}

	snprintf(target, sizeof(target),
	    "%s/%s", datestr, basename(file));
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
	fputs("usage: mvd [-f fmt]\n", stderr);
	exit(1);
}

int
isdir(char *name)
{
	struct stat sb;

	if (stat(name, &sb) == -1)
		errx(1, "%s: %s.", name, strerror(errno));

	return S_ISDIR(sb.st_mode);
}

int
main(int argc, char **argv)
{
	char *fmt = "%Y%m";
	struct dirent *de;
	DIR *dirp;
	int opt;

	while ((opt = getopt(argc, argv, "f:")) != -1) {
		switch (opt) {
		case 'f':
			fmt = optarg;
			break;
		case 'h':
		default:
			usage();
		}
	}

	if ((dirp = opendir(".")) == NULL)
		errx(1, NULL);

	while ((de = readdir(dirp)) != NULL) {
		if (isdir(de->d_name))
			continue;
		if (mvd(de->d_name, fmt) == -1)
			errx(1, "%s", errstr);
	}

	closedir(dirp);
	return 0;
}
