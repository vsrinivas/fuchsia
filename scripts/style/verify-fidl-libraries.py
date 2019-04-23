#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import subprocess
import sys


FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # style
    os.path.abspath(__file__))))

VENDOR_REGEX = r'^%s/vendor/(\w+)' % FUCHSIA_ROOT

DEFAULT_NAMESPACES = [
    'fidl',
    'fuchsia',
    'test',
]

IGNORED_DIRS = ['scripts', 'tools']


def main():
    parser = argparse.ArgumentParser(
            description=('Checks that FIDL libraries in a given repo are '
                         'properly namespaced'))
    parser.add_argument('--repo', help='The repo to analyze', required=True)
    args = parser.parse_args()

    repo = os.path.abspath(args.repo)
    vendor_match = re.search(VENDOR_REGEX, repo)
    if vendor_match:
        vendor = vendor_match.group(1)
        namespaces = [vendor]
    else:
        namespaces = DEFAULT_NAMESPACES

    files = subprocess.check_output(['git', '-C', repo, 'ls-files', '*.fidl'])

    has_errors = False
    for file in files.splitlines():
        if any(file.startswith(ignored) for ignored in IGNORED_DIRS):
          continue
        elif file.endswith('test.fidl'):
          continue

        with open(os.path.join(repo, file), 'r') as fidl:
            contents = fidl.read()
            result = re.search(r'^library ([^\.;]+)[^;]*;$', contents,
                               re.MULTILINE)
            if not result:
                print('Missing library declaration (%s)' % file)
                has_errors = True
                continue
            namespace = result.group(1)
            if namespace not in namespaces:
                print(
                    'Invalid namespace %s (%s), namespace must begin with one of [%s].'
                    % (namespace, file, ', '.join(namespaces)))
                has_errors = True

    return 1 if has_errors else 0


if __name__ == '__main__':
    sys.exit(main())
