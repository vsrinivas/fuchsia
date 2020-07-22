#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import subprocess
import sys
import shlex
import tempfile


def usage():
    # It is expected that this script is run by `fx core-tests` so there's
    # limited explanation here.
    print(
        'run-core-tests.py emu_command fuchsia_build_dir .../zbi_tests.json [gtest-filter]',
        file=sys.stderr)
    return 1


def main():
    _ = sys.argv.pop(0)
    emu_command = sys.argv.pop(0)
    fuchsia_build_dir = sys.argv.pop(0)
    zbi_tests = sys.argv.pop(0)
    test_filter = sys.argv.pop(0) if sys.argv else None
    if sys.argv:
        return usage()

    with open(zbi_tests, 'r') as f:
        tests = json.loads(f.read())

    path = [t['path'] for t in tests if t['name'] == 'core-tests']
    if not path:
        print(
            'Could not find "core-tests" entry in zbi_tests.json. If you '
            'discover you need to add a --with to fx set, please update '
            'utest/core/README.md.',
            file=sys.stderr)
        return 1

    filter_str = '--gtest_filter=' + (test_filter if test_filter else '*')
    args = [
        emu_command, '--headless', '--experiment-arm64', '-c', filter_str, '-z',
        os.path.join(fuchsia_build_dir, path[0])
    ]
    print('Running: %s' % ' '.join(map(shlex.quote, args)))
    subprocess.call(args)


if __name__ == '__main__':
    sys.exit(main())
