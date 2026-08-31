#! /usr/bin/env python3

import argparse
import os
import sys


class HumanSize:
    TB = 1 << 40
    GB = 1 << 30
    MB = 1 << 20
    KB = 1 << 10

    def __init__(self, value):
        self.value = int(value)

    def __str__(self):
        if self.value >= self.TB:
            return f"{(self.value/self.TB):7.2f}T"
        elif self.value >= self.GB:
            return f"{(self.value/self.GB):7.2f}G"
        elif self.value >= self.MB:
            return f"{(self.value/self.MB):7.2f}M"
        elif self.value >= self.KB:
            return f"{(self.value/self.KB):7.2f}K"
        else:
            return f"{self.value:7d}"

    @staticmethod
    def parse_size(value):
        try:
            return int(value)
        except ValueError:
            suffix = value[-1:].lower()
            number = int(value[:-1])

            multipliers = {
                "t": 1 << 40,
                "g": 1 << 30,
                "m": 1 << 20,
                "k": 1 << 10,
            }

            try:
                return round(number * multipliers[suffix])
            except KeyError:
                raise ValueError(f"Invalid size: {value}")


class FileInfo:
    def __init__(self, name, size):
        self.name = name
        self.size = size

    def __str__(self):
        return f"{HumanSize(self.size)}  {self.name}"

    def __lt__(self, other):
        return self.size < other.size


class Bucket:
    next_id = 1

    def __init__(self, capacity):
        self.capacity = capacity
        self.remaining = capacity
        self.contents = []
        self.id = Bucket.next_id
        Bucket.next_id += 1

    def can_fit(self, file_info):
        return file_info.size <= self.remaining

    def add(self, file_info):
        if not self.can_fit(file_info):
            return False
        self.contents.append(file_info)
        self.remaining -= file_info.size
        return True

    def percent_free(self):
        percent = int(self.remaining / self.capacity * 100.0)
        return f"{percent:3d}%"

    def __str__(self):
        lines = [
            f"=> Bucket {self.id:03d}, {self.percent_free()} free:",
        ]

        lines.extend(str(file_info) for file_info in self.contents)

        return "\n".join(lines)


def fit(file_list, capacity):
    buckets = []

    for file_info in file_list:
        if file_info.size > capacity:
            size = str(HumanSize(file_info.size)).strip()
            raise ValueError(f"Can never fit {file_info.name} ({size})")

        for bucket in buckets:
            if bucket.add(file_info):
                break
        else:
            bucket = Bucket(capacity)
            bucket.add(file_info)
            buckets.append(bucket)

    return buckets


def main():
    parser = argparse.ArgumentParser(description="Fit files into buckets.")

    parser.add_argument(
        "--capacity", type=str, default="15M", help="Bucket capacity."
    )

    parser.add_argument(
        "--num-buckets",
        action="store_true",
        default=False,
        help="Only show the number of buckets it will take.",
    )

    parser.add_argument(
        "--link-dest",
        type=str,
        default="",
        help="Link files into this basedir.",
    )

    parser.add_argument(
        "path",
        nargs="*",
        type=str,
        default=".",
        help="Find files in these paths.",
    )

    args = parser.parse_args()
    args.capacity = HumanSize.parse_size(args.capacity)

    file_list = []
    for path in args.path:
        for root, dirs, files in os.walk(path):
            for file in files:
                fullname = os.path.join(root, file)
                size = os.stat(fullname).st_size
                file_list.append(FileInfo(fullname, size))

    file_list.sort(reverse=True)

    try:
        buckets = fit(file_list, args.capacity)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1

    if args.num_buckets:
        print(f"{len(buckets)} buckets.")
        return 0

    for bucket in buckets:
        if args.link_dest:
            for file_info in bucket.contents:
                source = file_info.name
                destination = os.path.join(
                    args.link_dest, f"{bucket.id:03d}", source
                )
                os.makedirs(
                    os.path.dirname(destination), mode=0o700, exist_ok=True
                )
                os.link(source, destination)
        else:
            print(f"{bucket}\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
