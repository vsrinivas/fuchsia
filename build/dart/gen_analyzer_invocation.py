#!/usr/bin/env python2.7
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import stat
import string
import sys


def main():
    parser = argparse.ArgumentParser(
        description='Generate a script that invokes the Dart analyzer')
    parser.add_argument(
        '--out', help='Path to the invocation file to generate', required=True)
    parser.add_argument(
        '--source-file', help='Path to the list of sources', required=True)
    parser.add_argument(
        '--dot-packages', help='Path to the .packages file', required=True)
    parser.add_argument(
        '--dartanalyzer',
        help='Path to the Dart analyzer executable',
        required=True)
    parser.add_argument(
        '--dart-sdk', help='Path to the Dart SDK', required=True)
    parser.add_argument(
        '--package-name', help='Name of the analyzed package', required=True)
    parser.add_argument('--options', help='Path to analysis options')
    args = parser.parse_args()

    with open(args.source_file, 'r') as source_file:
        sources = source_file.read().strip().split('\n')

    analyzer_file = args.out
    analyzer_path = os.path.dirname(analyzer_file)
    if not os.path.exists(analyzer_path):
        os.makedirs(analyzer_path)

    script_template = string.Template(
        '''#!/bin/sh

echo "Package : $package_name"
$dartanalyzer \\
  --packages=$dot_packages \\
  --dart-sdk=$dart_sdk \\
  --fatal-warnings \\
  --fatal-hints \\
  --fatal-lints \\
  $options_argument \\
  $sources_argument \\
  "$$@"
''')
    with open(analyzer_file, 'w') as file:
        file.write(
            script_template.substitute(
                args.__dict__,
                package_name=args.package_name,
                sources_argument=' '.join(sources),
                options_argument='--options=' +
                args.options if args.options else ''))
    permissions = (
        stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR | stat.S_IRGRP |
        stat.S_IWGRP | stat.S_IXGRP | stat.S_IROTH)
    os.chmod(analyzer_file, permissions)


if __name__ == '__main__':
    sys.exit(main())
