#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Helper script for GN to run an arbitrary binary.

Run with:
  python3.8 gn_run_binary.py [--stamp <stamp_file>] <binary_name> [args ...]
"""

import os
import subprocess
import sys


def main(argv) -> int:

    # can't use argparse here since it gets confused if the binary being called
    # has its own --stamp option.
    if argv and argv[0] == '--stamp':
        stamp_file = argv[1]
        parameters = argv[2:]
    else:
        stamp_file = None
        parameters = argv

    # This script is designed to run binaries produced by the current build. We
    # may prefix it with "./" to avoid picking up system versions that might
    # also be on the path.
    path = parameters[0]
    if not os.path.isabs(path):
        path = './' + path

    # The rest of the arguments are passed directly to the executable.
    exec_args = [path] + parameters[1:]

    rc = subprocess.call(exec_args)
    if not rc and stamp_file:
        with open(stamp_file, 'w') as f:
            os.utime(f.name, None)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
