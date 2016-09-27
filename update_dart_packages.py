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
# error our and the conflicts will have to be resolved before the packages can
# be updated.
ROOT_PUBSPECS = [
    'lib/flutter/packages/flutter_test',
    'lib/flutter/packages/flutter_tools',
]


def main():
    flutter_path = os.path.join(paths.FUCHSIA_ROOT, 'lib/flutter')
    flutter_bin_path = os.path.join(flutter_path, 'bin')
    # First, run flutter precache to seed its dart SDK and packages
    # TODO: Stop relying on flutter's copy of the 'pub' binary. Instead, we
    # should build the 'dart' binary and use it to run pub.dart
    subprocess.check_call(
        [os.path.join(flutter_bin_path, 'flutter'), 'precache'])
    # Then run importer.py, placing the flutter bin cache on the PATH to pick
    # up the 'pub' binary
    importer_path = os.path.join(paths.FUCHSIA_ROOT, 'third_party', 'dart-pkg',
                                 'importer', 'importer.py')
    args = [importer_path]
    env = os.environ
    env['PATH'] += ":" + flutter_bin_path
    env['PATH'] += ":" + \
        os.path.join(flutter_bin_path, 'cache', 'dart-sdk', 'bin')
    env['FLUTTER_ROOT'] = flutter_path
    for root in ROOT_PUBSPECS:
        args.append(os.path.join(paths.FUCHSIA_ROOT, root))
    subprocess.check_call(args, env=env)

if __name__ == '__main__':
    sys.exit(main())
