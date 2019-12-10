#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys


def read_contents(manifest):
    with open(manifest, 'r') as manifest_file:
        lines = manifest_file.readlines()
        return dict(map(lambda l: l.strip().split('=', 1), lines))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--generated', help='Path to the generated manifest', required=True)
    parser.add_argument(
        '--reference', help='Path to the reference manifest', required=True)
    parser.add_argument('--stamp', help='Path to the stamp file', required=True)
    args = parser.parse_args()

    items_gen = read_contents(args.generated)
    items_ref = read_contents(args.reference)

    missing_keys_ref = [k for k in items_gen if k not in items_ref]
    missing_keys_gen = [k for k in items_ref if k not in items_gen]
    different_keys = [
        k for k in items_gen if k in items_ref and items_gen[k] != items_ref[k]
    ]

    if not missing_keys_gen and not missing_keys_ref and not different_keys:
        with open(args.stamp, 'w') as stamp_file:
            stamp_file.write('Comparison successful \o/')
        return 0

    print('------------------------------------------------------------------')
    print(
        'This build step failed because the Zircon and Fuchsia builds are '
        'out of sync.')

    if missing_keys_gen:
        print('')
        print("Items not in generated manifest")
        for item in sorted(missing_keys_gen):
            print('- ' + item)
        print('')
        print(
            'For items missing from the generated manifest, augment the '
            'appropriate target in //build/unification/images/BUILD.gn '
            'with a dependency on the missing item\'s target. For example, '
            'if "bin/foobar" is missing, just add a dependency on the '
            '":bin.foobar" target.')
        print(
            'Note that these targets are generated from metadata produced '
            'by the Zircon build. If the target does not exist, please '
            'verify that its original version in the Zircon build is '
            'declared with a target of type "zx_something".')

    if missing_keys_ref:
        print('')
        print("Items not in reference manifest")
        for item in sorted(missing_keys_ref):
            print('- ' + item)
        print('')
        print(
            'For items not in the reference manifest, inspect the '
            'dependencies of the failing target in '
            '//build/unification/images/BUILD.gn and remove the extraneous '
            'one.')

    if different_keys:
        print('')
        print("Items with different paths")
        for item in different_keys:
            print('- ' + item)
            print('   generated: ' + items_gen[item])
            print('   reference: ' + items_ref[item])
        print('')
        print(
            'If the generated and reference manifests cannot agree on the '
            'path of a given object, then something is busted in the Zircon '
            'build. Please see //build/unification/OWNERS for a list of '
            'folks who can help.')

    print('------------------------------------------------------------------')

    return 1


if __name__ == '__main__':
    sys.exit(main())
