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
    os.path.dirname(             # dart-pub
    os.path.abspath(__file__)))))

sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'pyyaml', 'lib')]
import yaml
sys.path += [os.path.join(FUCHSIA_ROOT, 'scripts', 'sdk', 'common')]
from frontend import Frontend


class DartBuilder(Frontend):

    def __init__(self, **kwargs):
        super(DartBuilder, self).__init__(**kwargs)

    def install_dart_library_atom(self, atom):
        base = self.dest('packages', atom['name'])
        # Copy all the source files.
        for file in atom['sources']:
            relative_path = os.path.relpath(file, atom['root'])
            dest = self.dest(base, relative_path)
            shutil.copy2(self.source(file), dest)
        # Gather the list of dependencies.
        # All Fuchsia dependencies are assumed to be siblings of this atom's
        # directory.
        deps = {}
        for dep in atom['deps']:
            deps[dep] = {
                'path': '../%s' % dep,
            }
        # Add third-party dependencies.
        for dep in atom['third_party_deps']:
            name = dep['name']
            version = dep['version']
            if name == 'flutter_sdk':
                deps[name] = {
                    'sdk': 'flutter',
                }
            else:
                deps[name] = '^%s' % version
        pubspec = {
            'name': atom['name'],
            'dependencies': deps,
        }
        manifest = os.path.join(base, 'pubspec.yaml')
        self.make_dir(manifest)
        with open(manifest, 'w') as manifest_file:
            yaml.safe_dump(pubspec, manifest_file, default_flow_style=False)


def main():
    parser = argparse.ArgumentParser(
            description='Lays out a Dart SDK for a given SDK tarball.')
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument('--archive',
                              help='Path to the SDK archive to ingest',
                              default='')
    source_group.add_argument('--directory',
                              help='Path to the SDK directory to ingest',
                              default='')
    parser.add_argument('--output',
                        help='Path to the directory where to install the SDK',
                        required=True)
    args = parser.parse_args()

    # Remove any existing output.
    shutil.rmtree(args.output, ignore_errors=True)

    builder = DartBuilder(archive=args.archive,
                          directory=args.directory,
                          output=args.output)
    return 0 if builder.run() else 1


if __name__ == '__main__':
    sys.exit(main())
