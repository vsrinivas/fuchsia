#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


from pathlib import Path
import os
import sys


def main():
    for path in Path(os.environ["FUCHSIA_DIR"]).rglob("*.fidl"):
        if not path.is_file():
            continue
        with open(path) as file:
            try:
                check_file(file)
            except UnicodeDecodeError:
                print(f"SKIPPING {path}: not unicode")


def check_file(file):
    line_parser = top_level
    for index, line in enumerate(file):
        if "//" in line:
            line = line[:line.index("//")]
        line = line.strip()
        result = line_parser(line)
        if result:
            line_parser, output = result
            if output:
                info = "" if file.name.endswith(".test.fidl") else "*** "
                info += f"{file.name}:{index + 1}"
                print(f"{info}: {output}")


def top_level(line):
    if line.startswith("union "):
        if "{" in line and "};" in line and "?" not in line:
            return
        if line.endswith("{"):
            return in_union, None
        return top_level, "unexpected formatting"

    keywords = "struct", "xunion", "enum", "bits", "table", "protocol"
    if any(line.startswith(k + " ") for k in keywords):
        if "{" in line and "};" in line:
            return
        if not line.endswith("{"):
            return top_level, "unexpected formatting"
        return in_other, None


def in_union(line):
    if line.startswith("}"):
        assert line == "};"
        return top_level, None
    if "?" in line:
        return in_union, "nullable union field"


def in_other(line):
    if line.startswith("}"):
        assert line == "};"
        return top_level, None


if __name__ == "__main__":
    main()
