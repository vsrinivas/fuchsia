#!/usr/bin/env fuchsia-vendored-python
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file

import argparse
import sys
import json
import os

# Keep in sync with //build/bazel/toplevel.WORKSPACE.bazel.
_BAZEL_INPUT_WORKSPACE_ROOT = 'external/legacy_ninja_build_outputs'


def main():
    parser = argparse.ArgumentParser(
        'Modifies paths from input images config JSON so they are usable by Bazel'
    )
    parser.add_argument(
        '--images-config', required=True, type=argparse.FileType('r'))
    parser.add_argument(
        '--key-dir',
        required=True,
        help='Directory where key files are located')
    parser.add_argument('--output', required=True, type=argparse.FileType('w'))
    args = parser.parse_args()

    config = json.load(args.images_config)
    vbmeta = find_vbmeta(config)
    if vbmeta:
        vbmeta['key'] = f'{_BAZEL_INPUT_WORKSPACE_ROOT}/{args.key_dir}/key.pem'
        vbmeta[
            'key_metadata'] = f'{_BAZEL_INPUT_WORKSPACE_ROOT}/{args.key_dir}/key_metadata.bin'
    json.dump(config, args.output)


def find_vbmeta(images_config):
    for entry in images_config['images']:
        if entry['type'] == 'vbmeta':
            return entry
    return None


if __name__ == '__main__':
    sys.exit(main())
