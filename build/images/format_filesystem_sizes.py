#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import json
import os
import subprocess
import sys


class CmdSize(object):

    def __init__(self, name, cmd, limit, debug=None):
        self.name = name
        self.size = subprocess.check_output(cmd).strip()
        self.limit = limit
        self.debug = debug


class PathSize(object):

    def __init__(self, name, path, limit, debug=None):
        self.name = name
        self.size = os.path.getsize(path)
        self.limit = limit
        self.debug = debug


def main():
    parser = argparse.ArgumentParser(
        description='Format filesystem image sizes')
    parser.add_argument('--fvm-blk', help='Path to fvm.sparse.blk file')
    parser.add_argument('--blob-blk', help='Path to blob.blk file')
    parser.add_argument('--data-blk', help='Path to data.blk file')
    parser.add_argument('--fuchsia-zbi', help='Path to fuchsia.zbi file')
    parser.add_argument('--zedboot-zbi', help='Path to zedboot.zbi file')
    parser.add_argument('--fvm-tool', help='Path to fvm tool')
    parser.add_argument('--blobfs-tool', help='Path to blobfs tool')
    parser.add_argument('--minfs-tool', help='Path to minfs tool')
    parser.add_argument(
        '--max-fvm-contents-size', help='Total size limit for FVM')
    parser.add_argument(
        '--max-blob-contents-size',
        default='0',
        help='Maximum size for blob contents')
    parser.add_argument(
        '--max-blob-image-size',
        default='0',
        help='Maximum size for blob image')
    parser.add_argument(
        '--max-data-contents-size',
        default='0',
        help='Maximum size for data contents')
    parser.add_argument(
        '--max-data-image-size',
        default='0',
        help='Maximum size for data contents')
    parser.add_argument(
        '--max-fuchsia-zbi-size',
        default='0',
        help='Maximum size for fuchsia.zbi')
    parser.add_argument(
        '--max-zedboot-zbi-size',
        default='0',
        help='Maximum size for zedboot.zbi')
    parser.add_argument('--output', help='Path to output file')
    args = parser.parse_args()

    data_points = []
    if args.fvm_blk:
        data_points.append(
            CmdSize(
                'fvm/contents_size', [args.fvm_tool, args.fvm_blk, 'size'],
                args.max_fvm_contents_size))
    if args.blob_blk:
        blob_tool_prefix = [args.blobfs_tool, args.blob_blk]
        data_points.extend(
            [
                CmdSize(
                    'blob/contents_size',
                    blob_tool_prefix + ['used-data-size'],
                    args.max_blob_contents_size,
                    debug=(
                        'To debug, reproduce the build as per '
                        'http://go/fuchsia-infra/faq#repro-build-step,'
                        'then run `fx blobstats`')),
                CmdSize(
                    'blob/image_size', blob_tool_prefix + ['used-size'],
                    args.max_blob_image_size)
            ])
    if args.data_blk:
        minfs_tool_prefix = [args.minfs_tool, args.data_blk]
        data_points.extend(
            [
                CmdSize(
                    'data/contents_size',
                    minfs_tool_prefix + ['used-data-size'],
                    args.max_data_contents_size),
                CmdSize(
                    'data/image_size', minfs_tool_prefix + ['used-size'],
                    args.max_data_image_size)
            ])
    if args.fuchsia_zbi:
        data_points.append(
            PathSize(
                'fuchsia.zbi', args.fuchsia_zbi, args.max_fuchsia_zbi_size))
    if args.zedboot_zbi:
        data_points.append(
            PathSize(
                'zedboot.zbi', args.zedboot_zbi, args.max_zedboot_zbi_size))

    data = []
    for d in data_points:
        d_dict = {
            'name': d.name,
            'value': int(d.size),
            'limit': int(d.limit),
        }
        if d.debug:
            d_dict['debug_instructions'] = d.debug
        data.append(d_dict)

    with open(args.output, 'w') as f:
        json.dump(data, f, indent=2)


if __name__ == '__main__':
    sys.exit(main())
