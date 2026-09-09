#! /usr/bin/env python3

import argparse
import os
import random
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Run a command for each file in random order."
    )

    parser.add_argument(
        "-e", "--extension", type=str, help="Find files with this extension."
    )

    parser.add_argument(
        "-p", "--path", default=".", help="Find files in this path."
    )

    parser.add_argument(
        "command", nargs="+", help="Run this command for each file."
    )

    args = parser.parse_args()

    if args.extension:
        if not args.extension.startswith("."):
            args.extension = "." + args.extension
        args.extension = args.extension.lower()

    file_list = []
    for root, dirs, files in os.walk(args.path):
        for file in files:
            if args.extension:
                _, extension = os.path.splitext(file)
                if args.extension != extension.lower():
                    continue
            fullname = os.path.join(root, file)
            file_list.append(fullname)

    random.shuffle(file_list)

    for file in file_list:
        has_placeholder = any("{}" in arg for arg in args.command)
        if has_placeholder:
            command = [arg.replace("{}", file) for arg in args.command]
        else:
            command = args.command
            command.append(file)

        print(f"==> {" ".join(command)}")
        try:
            subprocess.run(command, check=True)
        except Exception as e:
            print(e, file=sys.stderr)
            return 1
        except:
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
