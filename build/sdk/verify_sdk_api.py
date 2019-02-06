#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser()
    # TODO(DX-685): make this argument required.
    parser.add_argument('--reference',
                        help='Path to the golden API file',
                        required=False)
    parser.add_argument('--manifest',
                        help='Path to the SDK manifest',
                        required=True)
    parser.add_argument('--updated',
                        help='Path to the API file to compute',
                        required=True)
    args = parser.parse_args()

    if not args.reference:
        # Nothing to do.
        with open(args.updated, 'w') as updated_file:
            updated_file.write('No API verification for this SDK :/')
        return 0

    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)

    ids = map(lambda a: a['id'], manifest['atoms'])
    # Ignore documentation for now, as we use some documentation atoms to carry
    # ids.
    # TODO(DX-954): re-add documentation atoms.
    ids = filter(lambda i: not i.startswith('sdk://docs'), ids)

    with open(args.updated, 'w') as updated_file:
        updated_file.write('\n'.join(ids))

    with open(args.reference, 'r') as reference_file:
        old_ids = map(lambda l: l.strip(), reference_file.readlines())

    new_id_set = set(ids)
    old_id_set = set(old_ids)
    added_ids = new_id_set - old_id_set
    removed_ids = old_id_set - new_id_set

    if added_ids:
        print('Elements added to SDK:')
        for id in sorted(added_ids):
            print(' - %s' % id)
    if added_ids:
        print('Elements removed from SDK:')
        for id in sorted(removed_ids):
            print(' - %s' % id)
    if removed_ids or added_ids:
        print('Error: SDK contents have changed!')
        print('Please acknowledge this change by copying %s into %s.'
              % (args.updated, args.reference))
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
