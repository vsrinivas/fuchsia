#!/usr/bin/env python3.8

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
from pathlib import Path
import shutil

params = argparse.ArgumentParser(
    description="Copy all files in a directory tree and touch a stamp file")
params.add_argument("source", type=Path)
params.add_argument("target", type=Path)
params.add_argument("stamp", type=Path)
params.add_argument("--ignore_pattern", action="append")
params.add_argument("--no_symlinks", action='store_true')
args = params.parse_args()

if args.target.is_file():
    args.target.unlink()
if args.target.is_dir():
    shutil.rmtree(args.target, ignore_errors=True)

ignore = None
if args.ignore_pattern:
    ignore = shutil.ignore_patterns(*args.ignore_pattern)

keep_symlinks = True
if args.no_symlinks:
    keep_symlinks = False

shutil.copytree(args.source, args.target, symlinks=keep_symlinks, ignore=ignore)

stamp = Path(str(args.stamp))
stamp.touch()
