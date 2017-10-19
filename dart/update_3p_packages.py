#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import paths
import subprocess
import sys

# These are the locations of pubspec files that are roots of the dependency
# graph.  If they contain conflicting requirements for a package 'pub get' will
# error out and the conflicts will have to be resolved before the packages can
# be updated.
ROOT_PUBSPECS = [
    'lib/flutter/examples/flutter_gallery',
    'lib/flutter/packages/flutter',
    'lib/flutter/packages/flutter_test',
    'lib/flutter/packages/flutter_tools',
    'third_party/dart/pkg/analysis_server',
    'third_party/dart/pkg/analyzer',
    'third_party/dart/pkg/analyzer_cli',
    'third_party/dart/pkg/kernel',
    'third_party/dart/pkg/telemetry',
    'third_party/dart/pkg/typed_mock',
    'third_party/flutter/sky/packages/sky_engine',
]

# These are the locations of yaml files listing the Dart dependencies of a git
# project.
PROJECT_DEPENDENCIES = [
    'build/dart',
    'topaz/app/chat',
    'topaz/app/dashboard',
    'topaz/app/xi',
    'topaz/public/dart',
    'topaz/tools',
]


def main():
    if sys.platform.startswith('linux'):
        platform = 'linux'
    elif sys.platform.startswith('darwin'):
        platform = 'mac'
    else:
        print('Unsupported platform: %s' % sys.platform)
        return 1
    pub_path = os.path.join(paths.FUCHSIA_ROOT, 'third_party', 'dart', 'tools',
                            'sdks', platform, 'dart-sdk', 'bin', 'pub')
    importer_path = os.path.join(paths.FUCHSIA_ROOT, 'scripts', 'dart',
                                 'package_importer.py')
    output_path = os.path.join(paths.FUCHSIA_ROOT, 'third_party', 'dart-pkg',
                               'pub')
    flutter_root = os.path.join(paths.FUCHSIA_ROOT, 'lib', 'flutter');

    args = [importer_path]
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

    subprocess.check_call(args, env={"FLUTTER_ROOT": flutter_root});


if __name__ == '__main__':
    sys.exit(main())
