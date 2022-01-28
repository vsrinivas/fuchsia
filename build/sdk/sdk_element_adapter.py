#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
import os

SUPPORTED_TYPES = ['host_tool', 'version_history']


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

    with open(args.manifest_in, 'r') as manifest_in:
        element_manifest = json.load(manifest_in)

    adapter = Adapter(
        element_meta, element_manifest, args.gn_label, args.meta_out)
    if adapter.element_type not in SUPPORTED_TYPES:
        return 'Unsupported element type "{}"'.format(adapter.type)

    with open(args.meta_out, 'w') as meta_out:
        json.dump(adapter.atom_meta(), meta_out, indent=2, sort_keys=True)

    with open(args.manifest_out, 'w') as manifest_out:
        json.dump(
            adapter.atom_manifest(), manifest_out, indent=2, sort_keys=True)


class Adapter(object):

    def __init__(self, element_meta, element_manifest, gn_label, meta_out):
        self.element_meta = element_meta
        self.gn_label = gn_label
        self.name = element_meta['data']['name']
        self.element_type = element_meta['data']['element_type'] if 'element_type' in element_meta['data'] else element_meta['data']['type']
        # Type-dependent fields
        if self.element_type == 'version_history':
            self.file_base = ''
            self.id = 'sdk://version_history'
        elif self.element_type == 'host_tool':
            self.file_base = os.path.join(
                'tools', element_meta['data']['host_arch'])
            self.id = 'sdk://{}/{}'.format(self.file_base, self.name)
            self.root = 'tools'

        self.files = [
            {
                'destination': os.path.join(self.file_base, file['dst']),
                'source': file['src'],
            } for file in element_manifest[:-1]
        ] + [
            {
                'destination':
                    os.path.join(self.file_base, element_manifest[-1]['dst']),
                'source':
                    meta_out,
            }
        ]

    def atom_meta(self):
        if self.element_type == 'version_history':
            # Version history is pure metadata, so meta_out is the same as meta_in.
            return self.element_meta

        return {
            'files': [file['destination'] for file in self.files[:-1]],
            'name': self.name,
            'root': self.root,
            'type': self.element_type
        }

    def atom_manifest(self):
        return {
            'atoms':
                [
                    {
                        'category': 'partner',
                        'deps': [],
                        'files': self.files,
                        'gn-label': self.gn_label,
                        'id': self.id,
                        'meta': self.files[-1]['destination'],
                        'plasa': [],
                        'type': self.element_type,
                    }
                ],
            'ids': [self.id]
        }


if __name__ == '__main__':
    sys.exit(main())
