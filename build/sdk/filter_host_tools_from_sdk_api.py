#!/usr/bin/env python3.8
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Remove host tools that do not belong to a specific host CPU architecture
From SDK API manifest files."""

import argparse
import errno
import json
import os
import shutil
import sys

_HOST_CPUS = ('x64', 'arm64')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--host_cpu',
        required=True,
        choices=_HOST_CPUS,
        help='Host CPU architecture name')
    parser.add_argument(
        '--input',
        type=argparse.FileType('r'),
        required=True,
        help='Input API manifest file')
    parser.add_argument(
        '--output',
        type=argparse.FileType('w'),
        required=True,
        help='Output API manifest file')

    args = parser.parse_args()

    # Any line that begins with this prefix is a candidate for rejection.
    candidate_line_prefix = 'sdk://tools/'

    # Any candidate line that begins with this prefix is accepted, all others
    # are rejected.
    accepted_line_prefix = candidate_line_prefix + args.host_cpu + '/'

    for line in args.input:
        if line.startswith(candidate_line_prefix) and \
            not line.startswith(accepted_line_prefix):
            continue
        args.output.write(line)

    return 0


if __name__ == '__main__':
    sys.exit(main())
