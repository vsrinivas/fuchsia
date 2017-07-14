#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import subprocess
import sys

from check_build_status import check_build_status, get_default_builder_name


def main():
    default_builder = get_default_builder_name()

    parser = argparse.ArgumentParser(
        '''Check the fuchsia waterfall build status for the specified builder,
and run 'jiri update' only when the build is currently green. All the extra
arguments are passed through to the 'jiri update' command.
''')
    parser.add_argument(
        '--builder',
        '-b',
        help='Name of the builder to check, e.g. %s' % default_builder,
        default=default_builder)
    args, extras = parser.parse_known_args()

    # Check the build status.
    if check_build_status(args.builder) != 0:
        print 'ERROR - The "%s" build is currently red.' % args.builder
        return 1

    # Run 'jiri update' command.
    jiri_path = os.path.join(paths.FUCHSIA_ROOT, '.jiri_root', 'bin', 'jiri')
    return subprocess.call(
        [jiri_path, 'update'] + extras, stdout=sys.stdout, stderr=sys.stderr)


if __name__ == '__main__':
    sys.exit(main())
