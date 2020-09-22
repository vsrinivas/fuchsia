#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import platform
import sys


def main():
    parser = argparse.ArgumentParser()
    # TODO(fxbug.dev/5535): make this argument required.
    parser.add_argument(
        '--reference', help='Path to the golden API file', required=False)
    parser.add_argument(
        '--manifest', help='Path to the SDK manifest', required=True)
    parser.add_argument(
        '--updated', help='Path to the API file to compute', required=True)
    parser.add_argument(
        '--warn',
        help='Whether API changes should only cause warnings',
        action='store_true')
    args = parser.parse_args()

    if not args.reference:
        # Nothing to do.
        with open(args.updated, 'w') as updated_file:
            updated_file.write('No API verification for this SDK :/')
        return 0

    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)

    ids = [a['id'] for a in manifest['atoms']]

    # Ignore images which are very architecture-dependent.
    # TODO(fxbug.dev/5824): remove this exception when obsolete.
    ids = [i for i in ids if not (i.startswith('sdk://images'))]

    with open(args.updated, 'w') as updated_file:
        updated_file.write('\n'.join(ids))

    with open(args.reference, 'r') as reference_file:
        old_ids = [l.strip() for l in reference_file.readlines()]

    # tools/arm64 should not exist on mac hosts
    # TODO(fxb/42999): remove when SDK transition is complete.
    if platform.mac_ver()[0]:
        old_ids = [i for i in old_ids if not i.startswith('sdk://tools/arm64')]

    ids = filter(lambda i: not i.startswith('sdk://fidl/zx'), ids)
    old_ids = filter(lambda i: not i.startswith('sdk://fidl/zx'), old_ids)

    ids = filter(lambda i: not i.startswith('sdk://fidl/zx'), ids)
    old_ids = filter(lambda i: not i.startswith('sdk://fidl/zx'), old_ids)

    new_id_set = set(ids)
    old_id_set = set(old_ids)
    added_ids = new_id_set - old_id_set
    removed_ids = old_id_set - new_id_set

    if added_ids:
        print('Elements added to SDK:')
        for id in sorted(added_ids):
            print(' - %s' % id)
    if removed_ids:
        print('Elements removed from SDK:')
        for id in sorted(removed_ids):
            print(' - %s' % id)
    if removed_ids or added_ids:
        type = 'Warning' if args.warn else 'Error'
        print('%s: SDK contents have changed!' % type)
        print('Please acknowledge this change by running:')
        print('  cp ' + args.updated + ' ' + args.reference)
        if not args.warn:
            return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
