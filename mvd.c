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
mvd(char *src, struct stat *sb, char *fmt)
{
	char dst[PATH_MAX], datestr[PATH_MAX];

	if (strftime(datestr, sizeof(datestr),
	    fmt, localtime(&sb->st_mtime)) == 0) {
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

	snprintf(dst, sizeof(dst), "%s/%s", datestr, basename(src));
	if (rename(src, dst) == -1) {
		snprintf(errstr, sizeof(errstr),
		    "rename %s to %s: %s.", src, dst, strerror(errno));
		return -1;
	}

	return 0;
}

void
usage(void)
{
	fputs("usage: mvd [-f fmt] directory\n", stderr);
	exit(1);
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

	if (optind >= argc)
		usage();

	if (chdir(argv[optind]) == -1)
		errx(1, "%s: %s", argv[optind], strerror(errno));

	if ((dirp = opendir(".")) == NULL)
		errx(1, NULL);

	while ((de = readdir(dirp)) != NULL) {
		struct stat sb;

		if (stat(de->d_name, &sb) == -1)
			errx(1, "%s: %s.", de->d_name, strerror(errno));

		if (S_ISDIR(sb.st_mode))
			continue;

		if (mvd(de->d_name, &sb, fmt) == -1)
			errx(1, "%s", errstr);
	}

	closedir(dirp);
	return 0;
}
