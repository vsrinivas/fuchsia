#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Compares two or more elf_sizes.json files.
"""

import argparse
import csv
import json
import os
import subprocess
import sys

from tabulate import tabulate


FIELDS = [
    "blob",
    "bss",
    "code",
    "data",
    "file",
    "memory",
    "rel",
    "rela",
    "relcount",
    "relr",
    "relro",
    "rodata",
    "zbi",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--field", choices=FIELDS, default="code")
    parser.add_argument("file", nargs="+", metavar="elf_sizes.json")
    args = parser.parse_args()

    files = {}
    totals = []

    def load(f):
        data = json.load(f)
        assert set(data["totals"].keys()) == set(FIELDS)
        for file in data["files"]:
            build_id = file["build_id"]
            if build_id:
                files.setdefault(file["path"], []).append(file)
        totals.append(data["totals"])

    for file in args.file:
        with open(file) as f:
            load(f)

    table = []
    header = ["path", "baseline"]
    for i, _ in enumerate(args.file[1:]):
        header.extend(["%s %d" % (args.field, i + 1), "ratio %d" % (i + 1)])

    def compute_row(name, data, field):
        if not field in data[0]:
            return [0] * len(data)
        baseline = data[0][field]
        row = [name, baseline]
        for f in data[1:]:
            row.extend([f[field], float(f[field]) / baseline if baseline else 0.0])
        return row

    for path in sorted(files.keys()):
        file = files[path]
        if len(file) < 2:
            print("%s not present in every elf_sizes.json" % path, file=sys.stderr)
            continue
        if len(set([f["build_id"] for f in file])) == 1:
            print("%s is unchanged" % path, file=sys.stderr)
            continue
        table.append(compute_row(path, file, args.field))

    table.append(compute_row("TOTAL", totals, args.field))

    print(tabulate(table, headers=header, floatfmt=".3f"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
