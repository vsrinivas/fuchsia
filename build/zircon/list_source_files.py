#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import sys


FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
    os.path.dirname(             # zircon
    os.path.abspath(__file__))))
ZIRCON_ROOT = os.path.join(FUCHSIA_ROOT, "zircon")


def list_files(deleted=False):
    git_cmd = ['git', 'ls-files']
    if deleted:
        git_cmd.append('-d')
    output = subprocess.check_output(git_cmd, cwd=ZIRCON_ROOT)
    return set(output.splitlines())


def get_files():
    all_files = list_files()
    deleted_files = list_files(deleted=True)
    files = all_files - deleted_files
    return [os.path.join(ZIRCON_ROOT, file) for file in files]


def main():
    for file in get_files():
        print(file)


if __name__ == "__main__":
    sys.exit(main())
