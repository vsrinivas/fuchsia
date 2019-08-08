#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This script allows for the conversion between ids.txt and .build-id formats.
"""

import os
import sys
import argparse

# rel_to is expected to be absolute
def abs_path(path, rel_to):
    if os.path.isabs(path):
        return path
    else:
        return os.path.abspath(os.path.join(rel_to, path))

# rel_to is assumed to be absolute
def read_ids_txt(ids_path, rel_to):
    with open(ids_path) as f:
        return {build_id:abs_path(path, rel_to) for (build_id, path) in (x.split() for x in f.readlines())}

# build_id_dir is assumed to be absolute
def read_build_id_dir(build_id_dir):
    out = {}
    for root, dirs, files in os.walk(build_id_dir):
        if len(files) != 0 and len(dirs) != 0:
            raise Exception("%s is not a valid .build-id directory" % build_id_dir)
        for f in files:
            out[os.path.basename(root) + f] = os.path.join(root, f)
    return out

def link(src, dst):
    src = os.path.realpath(src)
    if os.path.exists(dst):
        os.remove(dst)
    os.link(src, dst)

def mkdir(path):
    try:
        os.makedirs(path)
    except OSError as e:
        if e.errno != os.errno.EEXIST:
            raise e

def touch(path):
    if os.path.exists(path):
        os.utime(path, None)
    else:
        with open(path, 'w'):
            return

def write_build_id_dir(build_id_dir, mods):
    for build_id, path in mods.iteritems():
        mkdir(os.path.join(build_id_dir, build_id[:2]))
        link(path, os.path.join(build_id_dir, build_id[:2], build_id[2:] + ".debug"))

# rel_to and path are assumed to be absolute
# if rel_to is None fix_path returns the absolute path. If rel_to
# is not None it turns the path into a relative path.
def fix_path(path, rel_to):
    if rel_to is None:
        return path
    return os.path.relpath(path, rel_to)

# rel_to is assumed to be an absolute path
def write_ids_txt(ids_path, rel_to, mods):
    with open(ids_path, "w") as f:
        for build_id, path in sorted(mods.iteritems()):
            path = fix_path(mods[build_id], rel_to)
            f.write("%s %s\n" % (build_id, path))

def main():
    ids_fmt = "ids.txt"
    build_id_fmt = ".build-id"

    parser = argparse.ArgumentParser(description="Convert between ids.txt and .build-id")
    parser.add_argument("-O", "--output-format", help="Sets the output format.",
                        metavar="FMT",
                        choices=[ids_fmt, build_id_fmt])
    parser.add_argument("--ids-rel-to-in",
                        help="When reading ids.txt use paths relative to DIR",
                        metavar="DIR")
    parser.add_argument("--ids-rel-to-out",
                        help="When writing ids.txt use paths relative to DIR",
                        metavar="DIR")
    parser.add_argument("--stamp",
                        help="Touch STAMP after finishing",
                        metavar="STAMP")
    parser.add_argument("input")
    parser.add_argument("output")

    args = parser.parse_args()

    input_path = os.path.abspath(args.input)
    output_path = args.output
    in_fmt = build_id_fmt if os.path.isdir(input_path) else ids_fmt
    rel_to_in = os.path.abspath(args.ids_rel_to_in) if args.ids_rel_to_in is not None else None
    rel_to_out = os.path.abspath(args.ids_rel_to_out) if args.ids_rel_to_out is not None else None

    if in_fmt == ids_fmt:
        if rel_to_in is None:
          rel_to_in = os.path.abspath(os.path.dirname(input_path))
        mods = read_ids_txt(input_path, rel_to_in)
    else:
        mods = read_build_id_dir(input_path)

    if args.output_format == None:
        if in_fmt == ids_fmt:
            out_fmt = build_id_fmt
        else:
            out_fmt = ids_fmt
    else:
        out_fmt = args.output_format

    if out_fmt == ids_fmt:
        write_ids_txt(output_path, rel_to_out, mods)
    else:
        write_build_id_dir(output_path, mods)

    if args.stamp is not None:
        touch(args.stamp)

if __name__ == "__main__":
    main()
