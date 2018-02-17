#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import namedtuple
import argparse
import sys


manifest_file = namedtuple('manifest_file', ['manifest', 'packages'])


class manifest_package_action(argparse.Action):
    def __init__(self, *args, **kwargs):
        super(manifest_package_action, self).__init__(*args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        manifests = getattr(namespace, self.dest, None)
        if manifests is None:
            manifests = []
            setattr(namespace, self.dest, manifests)
        manifests.append(manifest_file(values[0], values[1:]))


def main():
    parser = argparse.ArgumentParser("Write a package manifest")
    parser.add_argument("--manifest", nargs='+', action=manifest_package_action,
                        help="Name of the manifest file and its content")
    args = parser.parse_args()

    for file in args.manifest:
        with open(file.manifest, 'w') as f:
            for package in file.packages:
                f.write(package + '\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
