# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config',
                        help='The path to the list of components in the package',
                        required=True)
    parser.add_argument('--package',
                        help='The path to the Fuchsia package',
                        required=True)
    subparse = parser.add_subparsers().add_parser('run')
    subparse.add_argument('component',
                          nargs=1)
    args = parser.parse_args()

    with open(args.config, 'r') as config_file:
        components = config_file.readlines()

    component = args.component[0]
    if component not in components:
        print('Error: "%s" not in %s' % (component, components))
        return 1

    print('One day I will run %s from %s' % (component, args.package))

    return 0


if __name__ == '__main__':
    sys.exit(main())
