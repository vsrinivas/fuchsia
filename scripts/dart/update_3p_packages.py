#!/usr/bin/env python2.7
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import subprocess
import sys

# These are the locations of pubspec files that are roots of the dependency
# graph.  If they contain conflicting requirements for a package 'pub get' will
# error out and the conflicts will have to be resolved before the packages can
# be updated.
ROOT_PUBSPECS = [
    'third_party/dart/pkg/analysis_tool',
    'third_party/dart/pkg/build_integration',
    'third_party/dart/pkg/expect',
    'third_party/dart/pkg/testing',
    'third_party/dart-pkg/git/flutter/packages/flutter',
    'third_party/dart-pkg/git/flutter/packages/flutter_driver',
    'third_party/dart-pkg/git/flutter/packages/flutter_test',
    'third_party/dart-pkg/git/flutter/packages/flutter_tools',
    'prebuilt/third_party/sky_engine',
]

# These are the locations of yaml files listing the Dart dependencies of a git
# project.
PROJECT_DEPENDENCIES = [
    'sdk/testing/sl4f/client',
    'src/testing',
    'topaz/public/dart',
    'topaz/public/lib',
    'topaz/tools',
]


def main():
    parser = argparse.ArgumentParser('Update third-party Dart packages')
    parser.add_argument('--changelog',
                        help='Path to the changelog file to write',
                        default=None)
    parser.add_argument('--debug',
                        help='Turns on debugging mode',
                        action='store_true')
    script_args = parser.parse_args()

    if sys.platform.startswith('linux'):
        platform = 'linux-x64'
    elif sys.platform.startswith('darwin'):
        platform = 'mac-x64'
    else:
        print('Unsupported platform: %s' % sys.platform)
        return 1
    pub_path = os.path.join(paths.FUCHSIA_ROOT, 'prebuilt', 'third_party',
                            'dart', platform, 'bin', 'pub')
    importer_path = os.path.join(paths.FUCHSIA_ROOT, 'scripts', 'dart',
                                 'package_importer.py')
    output_path = os.path.join(paths.FUCHSIA_ROOT, 'third_party', 'dart-pkg',
                               'pub')
    flutter_root = os.path.join(paths.FUCHSIA_ROOT, 'third_party', 'dart-pkg',
                                'git', 'flutter')

    # flutter --version has the side effect of creating a version file that pub
    # uses to find which package versions are compatible with the current version
    # of flutter
    flutter_tool = os.path.join(flutter_root, 'bin', 'flutter')
    subprocess.check_call([flutter_tool, "--version"])

    args = [importer_path]
    if script_args.debug:
        args.append('--debug')
    args.append('--pub')
    args.append(pub_path)
    args.append('--output')
    args.append(output_path)
    args.append('--pubspecs')
    for root in ROOT_PUBSPECS:
        args.append(os.path.join(paths.FUCHSIA_ROOT, root))
    args.append('--projects')
    for project in PROJECT_DEPENDENCIES:
        args.append(os.path.join(paths.FUCHSIA_ROOT, project))
    if script_args.changelog:
        args.extend([
            '--changelog',
            script_args.changelog,
        ])

    subprocess.check_call(args, env={"FLUTTER_ROOT": flutter_root})


if __name__ == '__main__':
    sys.exit(main())
