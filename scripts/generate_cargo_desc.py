#!/usr/bin/env python2.7
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
from os import path
import platform
import subprocess
import sys


def extract_build_graph(gn_binary, out_dir):
    args = [
        gn_binary, 'desc', out_dir, '//*', '--format=json', '--all-toolchains'
    ]
    return subprocess.check_output(args)


def generate_depfile(outpath, out_dir):
    reloutpath = path.relpath(outpath, out_dir)
    # Just lie and say we depend on build.ninja so we get re-run every gen.
    # Despite the lie, this is more or less correct since we want to observe
    # every build graph change.
    return "%s: build.ninja" % reloutpath


def write_if_changed(outpath, content):
    """
    Writes content to the file named outpath.

    If outpath already exists and contains content already, does nothing and
    does not bump the file modification time. This lets ninja skip downstream
    actions if we don't need to change anything.
    """
    try:
        with open(outpath, "rb") as f:
            existing_content = f.read()
        if content == existing_content:
            return
    except IOError:
        pass

    with open(outpath, "wb") as outfile:
        outfile.write(content)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root_build_dir", required=True)
    parser.add_argument("--fuchsia_dir", required=True)
    parser.add_argument("--gn_binary", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--depfile", required=True)
    args = parser.parse_args()

    fake_project_json = extract_build_graph(args.gn_binary, args.root_build_dir)
    write_if_changed(args.output, fake_project_json)

    depfile = generate_depfile(args.output, args.root_build_dir)
    with open(args.depfile, "wb") as outfile:
        outfile.write(depfile)
    return 0


if __name__ == "__main__":
    sys.exit(main())
