#! /usr/bin/env python3

import argparse
import datetime
import glob
import os
import shutil
import sys

def sanitize_string(string):
    forbidden = ( "/", "|", ":", "*", "?", "\"", "<", ">", "'", "\\" )
    return string.translate(str.maketrans({c: "_" for c in forbidden}))

def mvd(source, mtime, date_format, dry_run):
    date_string = sanitize_string(mtime.strftime(date_format))
    destination = os.path.join(date_string, source)
    if not dry_run:
        os.makedirs(date_string, exist_ok=True)
        shutil.move(source, destination)
    print(f"{source} -> {destination}")

def main():
    parser = argparse.ArgumentParser();
    parser.add_argument(
        "-f", "--date-format",
        type=str,
        default="%Y%m",
        help="Date format string in strftime format."
    )

    parser.add_argument(
        "-s", "--dry-run",
        action="store_true",
        default=False,
        help="Simulate a run."
    )

    parser.add_argument(
        "path",
        type=str,
        help="Directory to clean up."
    )

    args = parser.parse_args()

    os.chdir(args.path)
    for path in glob.glob("*"):
        if os.path.isfile(path):
            try:
                mvd(
                    path,
                    datetime.date.fromtimestamp(os.stat(path).st_mtime),
                    args.date_format,
                    args.dry_run
                )
            except Exception as e:
                print(e, file=sys.stderr)
                return 1

    return 0

if __name__ == "__main__":
    sys.exit(main())

