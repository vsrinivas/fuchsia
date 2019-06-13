#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description='Format FVM size')
    parser.add_argument('--fvm-blk', help='Path to fvm.sparse.blk file')
    parser.add_argument('--blob-blk', help='Path to blob.blk file')
    parser.add_argument('--data-blk', help='Path to data.blk file')
    parser.add_argument('--fvm-tool', help='Path to fvm tool')
    parser.add_argument('--blobfs-tool', help='Path to blobfs tool')
    parser.add_argument('--minfs-tool', help='Path to minfs tool')
    parser.add_argument('--max-fvm-contents-size',
                        help='Total size limit for FVM')
    parser.add_argument('--max-blob-contents-size', default='0',
                        help='Maximum size for blob contents')
    parser.add_argument('--max-blob-image-size',  default='0',
                        help='Maximum size for blob image')
    parser.add_argument('--max-data-contents-size', default='0',
                        help='Maximum size for data contents')
    parser.add_argument('--max-data-image-size', default='0',
                        help='Maximum size for data contents')
    parser.add_argument('--output', help='Path to output file')
    args = parser.parse_args()

    blob_tool_prefix = [args.blobfs_tool, args.blob_blk]
    minfs_tool_prefix = [args.minfs_tool, args.data_blk]

    # 'name', 'tool invocation', 'limit'
    data_points = [
        ['fvm/contents_size', [args.fvm_tool, args.fvm_blk, 'size'],
         args.max_fvm_contents_size],
        ['blob/contents_size', blob_tool_prefix + ['used-data-size'],
         args.max_blob_contents_size],
        ['blob/image_size', blob_tool_prefix + ['used-size'],
         args.max_blob_image_size],
        ['data/contents_size', minfs_tool_prefix + ['used-data-size'],
         args.max_data_contents_size],
        ['data/image_size', minfs_tool_prefix + ['used-size'],
         args.max_data_image_size]]

    data = []
    for d in data_points:
        value = subprocess.check_output(d[1]).strip()
        data.append({
            'name': d[0],
            'value': int(value),
            'limit': int(d[2])})

    with open(args.output, 'w') as f:
        json.dump(data, f, indent=2)


if __name__ == '__main__':
    sys.exit(main())
