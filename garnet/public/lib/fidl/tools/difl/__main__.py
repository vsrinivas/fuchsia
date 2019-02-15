#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse

from difl.ir import Libraries
from difl.library import library_changes
from difl.abi import abi_changes

ap = argparse.ArgumentParser(prog='difl')
ap.add_argument('old', type=str, help='the .fidl.json from before the change')
ap.add_argument('new', type=str, help='the .fidl.json from after the change')
args = ap.parse_args()

before = Libraries().load(args.old)
after = Libraries().load(args.new)

changes = library_changes(before, after)

hardness = {True: 'HARD', False: 'SOFT'}

for abi in abi_changes(changes):
    print('{}: {}'.format(hardness[abi.hard], abi.message))
