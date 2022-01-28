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
        '--depfile-path', help='A path to the depfile.', required=True)
    parser.add_argument(
        '--gn-label',
        help='GN label, with toolchain, of the SDK element adapter.',
        required=True)
    parser.add_argument(
        '--gn-meta-path',
        help='Path to a file containing GN collected metadata. '
        'See sdk_element_adapter() for the format.',
        required=True)
    parser.add_argument(
        '--atom-meta-path',
        help='Path to write .meta.json file to.',
        required=True)
    parser.add_argument(
        '--atom-manifest-path',
        help='Path to write .sdk file to.',
        required=True)
    parser.add_argument(
        '--root-build-dir',
        help='Root build dir used for rebasing the packaging manifest.',
        required=True)
    parser.add_argument(
        '--sdk-category', help='SDK atom category.', required=True)
    args = parser.parse_args()

    with open(args.gn_meta_path, 'r') as gn_meta_file:
        gn_meta = json.load(gn_meta_file)[0]  # expected to contain one item

    # Read the SDK element metadata file.
    element_meta_path = os.path.relpath(
        gn_meta['meta']['source'], args.root_build_dir)
    with open(element_meta_path, 'r') as element_meta_file:
        element_meta = json.load(element_meta_file)

    adapter = Adapter(
        element_meta, gn_meta, args.gn_label, args.root_build_dir,
        args.sdk_category, args.atom_meta_path).adapt()

    with open(args.atom_meta_path, 'w') as atom_meta_file:
        json.dump(adapter['meta'], atom_meta_file, indent=2, sort_keys=True)

    with open(args.atom_manifest_path, 'w') as atom_manifest_file:
        json.dump(
            adapter['manifest'], atom_manifest_file, indent=2, sort_keys=True)

    with open(args.depfile_path, 'w') as depfile_file:
        depfile_file.write(
            '{}: {}\n'.format(args.atom_meta_path, element_meta_path))


class Adapter(object):
    """The adapter generates SDK atom files from SDK element GN metadata."""

    def __init__(
            self, element_meta, gn_meta, gn_label, base_out_dir, category,
            atom_meta_path):
        """Initializes the adapter instances.

        Args:
            element_meta: The SDK element metadata parsed from JSON. The
            format is determined by the element type but in general it is
            always a versioned envelope. See
            //sdk/schema/common-00000000.json.
            gn_metadata: The sdk_element() GN collected metadata. See
            sdk_element GN template.
            gn_label: A GN label of the sdk_element_adapter() target.
            base_out_dir: The root of the GN output directory used to compute
            relative paths.
            category: The SDK category. See sdk_atom() template.
            atom_meta_path: A path of the atom metadata file that is created
            by this adapter. The path is relative to base_out_dir.
        """
        self.element_meta = element_meta
        self.gn_meta = gn_meta
        self.gn_label = gn_label
        self.base_out_dir = base_out_dir
        self.category = category
        self.atom_meta_path = atom_meta_path

        # Derived properties
        self.name = element_meta['data']['name']
        self.element_type = element_meta['data']['element_type'] if 'element_type' in element_meta['data'] else element_meta['data']['type']

    def adapt(self):
        """Generates an adapter dict containing manifest and metadata."""
        return getattr(self, '_adapt_{}'.format(self.element_type))()

    def _adapt_version_history(self):
        # Version history is pure metadata. There are no other files.
        # Moreover, the metadata format is shared between sdk_element and sdk_atom.
        # We can copy it as is.

        # Version history is written into the SDK root dir hence no base dir.
        dest_base = ''

        # Version history is pure metadata so return the metadata directly.
        return self._adapter(
            self.element_meta,
            self._manifest(
                'sdk://version_history',
                self._atom_manifest_files(dest_base, 'version_history.json')))

    def _adapt_host_tool(self):
        dest_root = 'tools'
        # Host tools end up in tools/${hos_arch} directories in the SDK
        dest_base = os.path.join(
            dest_root, self.element_meta['data']['host_arch'])

        id = 'sdk://{}/{}'.format(dest_base, self.name)

        meta = {
            'files': self._atom_meta_files(dest_base),
            'name': self.name,
            'root': dest_root,
            'type': self.element_type
        }

        return self._adapter(
            meta,
            self._manifest(
                id,
                self._atom_manifest_files(
                    dest_base, '{}-meta.json'.format(self.name))))

    def _atom_meta_files(self, dest_base):
        """Returns a list of files as it appears in the atom metadata.

        The list excludes the metadata file itself since it would result in a
        circular ref.

        Args:
          dest_base: The base output dir inside the SDK archive.

        Returns: A list of file paths inside the SDK archive.
        """
        return [
            os.path.join(dest_base, file['dest'])
            for file in self.gn_meta['files']
        ]

    def _atom_manifest_files(self, dest_base, dest_meta_path):
        """Return a list of mapping between build output and SDK archive.

        The list is as it appears in the atom manifest. It includes the atom
        metadata file.

        Args:
          dest_base: The base output dir inside the SDK archive.
          dest_meta_path: The name of the atom metadata path in the SDK archive
            relative to dest_base.

        Returns:
          A list of dicts where each dict contains 'source' and 'destination'
          with source being a path inside `build_out_dir` and destination a
          path inside the SDK archive.
        """
        # Include the metadata mapping as the *last* entry.
        # Please note the atom_meta_path is relative to base_out_dir already.
        # Thus it should not be rebased.
        return [
            {
                'destination': os.path.join(dest_base, file['dest']),
                'source': os.path.relpath(file['source'], self.base_out_dir),
            } for file in self.gn_meta['files']
        ] + [
            {
                'source': self.atom_meta_path,
                'destination': os.path.join(dest_base, dest_meta_path)
            }
        ]

    def _adapter(self, meta, manifest):
        return {
            'meta': meta,
            'manifest': manifest,
        }

    def _manifest(self, id, files):
        """Generates the SDK atom manifest.

        The manifest is written into the output dir with the .sdk extension.
        The manifest is used to package atoms into a single SDK archive.

        Args:
          id: The SDK atom ID.
          files: A list of dict where every dict contains 'source' and
            'destination' where the source is a path relative to the build
            output dir and destination is the location in the SDK relative
            to the SDK root. This list includes the SDK metadata.

        Returns: A dict corresponding to the JSON manifest.

        """
        return {
            'atoms':
                [
                    {
                        'category': self.category,
                        'deps': [],
                        'files': files,
                        'gn-label': self.gn_label,
                        'id': id,
                        'meta': files[-1]['destination'],  # We appended meta
                        'plasa': [],
                        'type': self.element_type,
                    }
                ],
            'ids': [id]
        }


if __name__ == '__main__':
    sys.exit(main())
