#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # sdk
    os.path.abspath(__file__))))
sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'pyyaml', 'lib')]
import yaml

from layout_builder import Builder, process_manifest

class DartBuilder(Builder):

    def __init__(self, output):
        super(DartBuilder, self).__init__(domains=['dart'])
        self.output = output

    def install_dart_atom(self, atom):
        base = os.path.join(self.output, 'packages', atom.id.name)
        # Copy all the source files.
        for file in atom.files:
            dest = os.path.join(base, file.destination)
            self.make_dir(dest)
            shutil.copyfile(file.source, dest)
        # Gather the list of dependencies.
        # All Fuchsia dependencies are assumed to be siblings of this atom's
        # directory.
        deps = {}
        for dep in atom.deps:
            name = str(dep.name)
            deps[name] = {
                'path': '../%s' % name,
            }
        # Add third-party dependencies which are already available in a separate
        # manifest file.
        deps['%s_third_party' % atom.id.name] = {
            'path': 'third_party',
        }
        pubspec = {
            'name': atom.id.name,
            'dependencies': deps,
        }
        manifest = os.path.join(base, 'pubspec.yaml')
        self.make_dir(manifest)
        with open(manifest, 'w') as manifest_file:
            yaml.safe_dump(pubspec, manifest_file, default_flow_style=False)


def main():
    parser = argparse.ArgumentParser(
            description=('Lays out a Dart SDK based on the given manifest'))
    parser.add_argument('--manifest',
                        help='Path to the SDK manifest',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the directory where to install the SDK',
                        required=True)
    args = parser.parse_args()

    shutil.rmtree(args.output, True)

    builder = DartBuilder(args.output)
    return 0 if process_manifest(args.manifest, builder) else 1


if __name__ == '__main__':
    sys.exit(main())
