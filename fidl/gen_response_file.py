#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string
import sys


def read_libraries(libraries_path):
    with open(libraries_path) as f:
        lines = f.readlines()
        return [l.rstrip("\n") for l in lines]


def write_libraries(libraries_path, libraries):
    directory = os.path.dirname(libraries_path)
    if not os.path.exists(directory):
        os.makedirs(directory)
    with open(libraries_path, "w+") as f:
        for library in libraries:
            f.write(library)
            f.write("\n")


def main():
    parser = argparse.ArgumentParser(description="Generate response file for FIDL frontend")
    parser.add_argument("--out-response-file", help="The path for for the response file to generate", required=True)
    parser.add_argument("--out-libraries", help="The path for for the libraries file to generate", required=True)
    parser.add_argument("--json", help="The path for the JSON file to generate, if any")
    parser.add_argument("--tables", help="The path for the tables file to generate, if any")
    parser.add_argument("--c-client", help="The path for the C simple client header file to generate, if any")
    parser.add_argument("--c-header", help="The path for the C header file to generate, if any")
    parser.add_argument("--name", help="The name for the generated FIDL library, if any")
    parser.add_argument("--sources", help="List of FIDL source files", nargs="*")
    parser.add_argument("--dep-libraries", help="List of dependent libraries", nargs="*")
    args = parser.parse_args()

    target_libraries = []

    for dep_libraries_path in args.dep_libraries or []:
        dep_libraries = read_libraries(dep_libraries_path)
        for library in dep_libraries:
            if library in target_libraries:
                continue
            target_libraries.append(library)

    target_libraries.append(" ".join(sorted(args.sources)))
    write_libraries(args.out_libraries, target_libraries)

    response_file = []

    if args.json:
        response_file.append("--json %s" % args.json)

    if args.tables:
        response_file.append("--tables %s" % args.tables)

    if args.c_client:
        response_file.append("--c-client %s" % args.c_client)

    if args.c_header:
        response_file.append("--c-header %s" % args.c_header)

    if args.name:
        response_file.append("--name %s" % args.name)

    response_file.extend(["--files %s" % library for library in target_libraries])

    with open(args.out_response_file, "w+") as f:
        f.write(" ".join(response_file))
        f.write("\n")


if __name__ == "__main__":
  sys.exit(main())
