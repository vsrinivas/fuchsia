#!/usr/bin/env python3.8
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--avbtool', help='path to avbtool provided by Android', required=True)
    parser.add_argument(
        '--image', help='path to test image to create', required=True)
    parser.add_argument(
        '--salt', help='salt to use when adding hash footer', required=True)
    parser.add_argument(
        '--output_vbmeta_image',
        help='path to test vbmeta image to output to',
        required=True)
    args = parser.parse_args()

    with open(args.image, 'w') as f:
        f.write('0123456789ABCDEF0123456789ABCDEF')

    process = subprocess.run(
        [
            args.avbtool,
            'add_hash_footer',
            '--image',
            args.image,
            '--salt',
            args.salt,
            '--partition_name',
            'zircon',
            '--do_not_append_vbmeta_image',
            '--output_vbmeta_image',
            args.output_vbmeta_image,
            '--partition_size',
            # we are not going to add footer into image,
            # so, we do not care about a partition size checking.
            # `partition_size' is a mandatory option, thus let
            # use obviously big number for the partition size to pass
            # verification. 200M should be good enough.
            # TODO(dmitryya@) fix avbtool to do not check partition
            # size if --do_not_append_vbmeta_image is specified.
            '209715200',
        ])
    return process.returncode


if __name__ == '__main__':
    sys.exit(main())
