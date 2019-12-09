#!/usr/bin/env python2.7
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
    parser = argparse.ArgumentParser(
        description="Generate response file for Banjo frontend")
    parser.add_argument(
        "--out-response-file",
        help="The path for for the response file to generate",
        required=True)
    parser.add_argument(
        "--out-libraries",
        help="The path for for the libraries file to generate",
        required=True)
    parser.add_argument(
        "--backend",
        help="The path for the C simple client file to generate, if any")
    parser.add_argument(
        "--output", help="The path for the C++ header file to generate, if any")
    parser.add_argument(
        "--name", help="The name for the generated Banjo library, if any")
    parser.add_argument(
        "--sources", help="List of Banjo source files", nargs="*")
    parser.add_argument(
        "--dep-libraries", help="List of dependent libraries", nargs="*")
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

    if args.name:
        response_file.append("--name %s" % args.name)

    if args.backend:
        response_file.append("--backend %s" % args.backend)

    if args.output:
        response_file.append("--output %s" % args.output)

    response_file.extend(
        ["--files %s" % library for library in target_libraries])

    with open(args.out_response_file, "w+") as f:
        f.write(" ".join(response_file))
        f.write("\n")


if __name__ == "__main__":
    sys.exit(main())
