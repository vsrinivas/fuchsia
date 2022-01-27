#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
import os


def main():
    """Writes SDK atom-format metadata based on SDK element metadata.

    This script should only be run via the sdk_element_adapter GN template.

    Returns:
      None if successful, otherwise a string describing the error.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--meta-in',
        help='Path to read sdk_metadata.json file from',
        required=True)
    parser.add_argument(
        '--manifest-in',
        help='Path to read packaging_manifest.json file from',
        required=True)
    parser.add_argument(
        '--meta-out', help='Path to write .meta.json file to', required=True)
    parser.add_argument(
        '--manifest-out', help='Path to write .sdk file to', required=True)
    parser.add_argument(
        '--gn-label',
        help='GN label, with toolchain, of the SDK element adapter',
        required=True)
    args = parser.parse_args()

    with open(args.meta_in, 'r') as meta_in:
        element_meta = json.load(meta_in)
    element_type = element_meta['data']['type']
    category = element_meta['data']['category'] if 'category' in element_meta[
        'data'] else 'partner'

    with open(args.manifest_in, 'r') as manifest_in:
        element_manifest = json.load(manifest_in)

    if element_type == 'version_history':
        # Version history is pure metadata, so meta_out is the same as meta_in.
        atom_meta = element_meta
        atom_id = "sdk://version_history"
    else:
        return 'Unsupported element type ' + element_meta['data']['type']

    files = [
        {
            'destination': file['dst'],
            'source': file['src'],
        } for file in element_manifest
    ]

    atom_manifest = {
        'atoms':
            [
                {
                    'category': category,
                    'deps': [],
                    'files': files,
                    'gn-label': args.gn_label,
                    'id': atom_id,
                    'meta': files[-1]['destination'],
                    'plasa': [],
                    'type': element_type,
                }
            ],
        'ids': [atom_id]
    }

    with open(args.meta_out, 'w') as meta_out:
        json.dump(atom_meta, meta_out, indent=2, sort_keys=True)

    with open(args.manifest_out, 'w') as manifest_out:
        json.dump(atom_manifest, manifest_out, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
