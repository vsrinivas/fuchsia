#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import argparse
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # src
    os.path.dirname(             # graphics
    os.path.dirname(             # lib
    os.path.dirname(             # goldfish-vulkan
    SCRIPT_DIR)))))              # gnbuild

sys.path += [os.path.join(FUCHSIA_ROOT, 'build', 'images')]
from elfinfo import get_elf_info


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source')
    parser.add_argument('--destination')
    args = parser.parse_args()
    get_elf_info(args.source).strip(args.destination)
    return 0


if __name__ == '__main__':
    sys.exit(main())
