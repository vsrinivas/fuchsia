#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import stat
import string
import sys

import label_to_package_name

def main():
  parser = argparse.ArgumentParser(
      description='Generate a script that invokes the Dart analyzer')
  parser.add_argument('--out', help='Path to the invocation file to generate',
                      required=True)
  parser.add_argument('--source-dir', help='Path to package source',
                      required=True)
  parser.add_argument('--dot-packages', help='Path to the .packages file',
                      required=True)
  parser.add_argument('--dartanalyzer',
                      help='Path to the Dart analyzer executable',
                      required=True)
  parser.add_argument('--dart-sdk', help='Path to the Dart SDK',
                      required=True)
  parser.add_argument('--package-name', help='Name of the analyzed package')
  parser.add_argument('--package-label', help='Label of the analyzed package')
  parser.add_argument('--options', help='Path to analysis options')
  parser.add_argument('extra_sources', nargs='*',
                      help='Extra source paths to analyze')
  args = parser.parse_args()

  if args.package_name:
      package_name = args.package_name
  else:
      package_name = label_to_package_name.convert(args.package_label)

  analyzer_file = args.out
  analyzer_path = os.path.dirname(analyzer_file)
  if not os.path.exists(analyzer_path):
    os.makedirs(analyzer_path)

  script_template = string.Template('''#!/bin/sh

echo "Package : $package_name"
$dartanalyzer \\
  --packages=$dot_packages \\
  --dart-sdk=$dart_sdk \\
  --fatal-warnings \\
  --fatal-hints \\
  --fatal-lints \\
  $options_argument \\
  $source_dir $extra_sources_string \\
  "$$@"
''')
  with open(analyzer_file, 'w') as file:
      file.write(script_template.substitute(
          args.__dict__,
          extra_sources_string = ' '.join(args.extra_sources),
          package_name = package_name,
          options_argument = '--options='+args.options if args.options else ''))
  permissions = (stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |
                 stat.S_IRGRP | stat.S_IWGRP | stat.S_IXGRP |
                 stat.S_IROTH)
  os.chmod(analyzer_file, permissions)


if __name__ == '__main__':
  sys.exit(main())
