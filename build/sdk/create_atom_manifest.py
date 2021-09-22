#!/usr/bin/env python3.8
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import itertools
import json
import os
import sys

from sdk_common import Atom, detect_category_violations, detect_collisions, gather_dependencies


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--id', help='The atom\'s identifier', required=True)
    parser.add_argument('--out', help='Path to the output file', required=True)
    parser.add_argument('--depfile', help='Path to the depfile', required=True)
    parser.add_argument(
        '--deps', help='List of manifest paths for dependencies', nargs='*')
    parser.add_argument(
        '--file',
        help='A (destination <-- source) mapping',
        action='append',
        nargs=2)
    parser.add_argument(
        '--file-list', help='A file containing destination=source mappings')
    parser.add_argument(
        '--gn-label', help='GN label of the atom', required=True)
    parser.add_argument('--category', help='Publication level', required=True)
    parser.add_argument(
        '--meta',
        help=
        'Path to the atom\'s metadata file in the SDK. Required by default unless --noop-atom is set to True.'
    )
    parser.add_argument(
        '--noop-atom',
        action='store_true',
        help=
        'Whether the atom is a sdk_noop_atom. Sets the atom\'s meta to be empty. Defaults to False.'
    )
    parser.add_argument('--type', help='Type of the atom', required=True)
    args = parser.parse_args()

    if args.meta is None and not args.noop_atom:
        parser.error("--meta is required.")

    # Gather the definitions of other atoms this atom depends on.
    (deps, atoms) = gather_dependencies(args.deps)

    # Build the list of files making up this atom.
    extra_files = []
    if args.file_list:
        with open(args.file_list, 'r') as file_list_file:
            extra_files = [
                line.strip().split('=', 1)
                for line in file_list_file.readlines()
            ]
    files = dict(itertools.chain(
        args.file, extra_files)) if args.file else dict(extra_files)

    atoms.update(
        [
            Atom(
                {
                    'id': args.id,
                    'meta': args.meta or '',
                    'gn-label': args.gn_label,
                    'category': args.category,
                    'deps': sorted(list(deps)),
                    'files':
                        [
                            {
                                'source': os.path.normpath(source),
                                'destination': os.path.normpath(destination)
                            } for destination, source in files.items()
                        ],
                    'type': args.type,
                })
        ])

    if detect_collisions(atoms):
        print('Name collisions detected!')
        return 1
    if detect_category_violations(args.category, atoms):
        print('Publication level violations detected!')
        return 1

    manifest = {
        'ids': [args.id],
        'atoms': [a.json for a in sorted(list(atoms))],
    }

    with open(os.path.abspath(args.out), 'w') as out:
        json.dump(
            manifest, out, indent=2, sort_keys=True, separators=(',', ': '))

    with open(args.depfile, 'w') as dep_file:
        dep_file.write(
            '{}: {}\n'.format(
                args.out,
                # Always write relative paths to depfiles. See more information
                # from https://fxbug.dev/75451.
                ' '.join(os.path.relpath(source) for _, source in extra_files),
            ),
        )


if __name__ == '__main__':
    sys.exit(main())
