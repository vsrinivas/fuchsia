#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse

from difl.ir import Libraries
from difl.library import libraries_changes
from difl.abi import abi_changes
from difl.text_output import text_output
from difl.tricium_output import tricium_output

ap = argparse.ArgumentParser(prog='difl')

before_group = ap.add_mutually_exclusive_group(required=True)
before_group.add_argument('--before', type=str, help='the .fidl.json from before the change')
before_group.add_argument('--before-files', type=str, help='a list of .fidl.json files from before the change')

after_group = ap.add_mutually_exclusive_group(required=True)
after_group.add_argument('--after', type=str, help='the .fidl.json from after the change')
after_group.add_argument('--after-files', type=str, help='a list of .fidl.json files from after the change')

ap.add_argument('--format', choices=['text', 'tricium'], default='text')
args = ap.parse_args()

before_libraries = Libraries()
if args.before:
    before_libraries.load(args.before)
else:
    before_libraries.load_all(args.before_files)

after_libraries = Libraries()
if args.after:
    after_libraries.load(args.after)
else:
    after_libraries.load_all(args.after_files)

changes = libraries_changes(before_libraries, after_libraries, {})

classified_changes = abi_changes(changes)

if args.format == 'text':
    text_output(classified_changes)
elif args.format == 'tricium':
    tricium_output(classified_changes)
