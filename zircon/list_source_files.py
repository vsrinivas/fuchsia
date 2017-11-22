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


def get_files():
    files = subprocess.check_output(['git', 'ls-files'], cwd=ZIRCON_ROOT)
    return [os.path.join(ZIRCON_ROOT, file) for file in files.splitlines()]


def main():
    for file in get_files():
        print(file)


if __name__ == "__main__":
    sys.exit(main())
