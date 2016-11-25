#!/usr/bin/env python
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
      'Generate a script that invokes the Dart analyzer')
  parser.add_argument('--out', help='Path to the invocation file to generate',
                      required=True)
  parser.add_argument('--source-dir', help='Path to package source',
                      required=True)
  parser.add_argument('--dot-packages', help='Path to the .packages file',
                      required=True)
  parser.add_argument('--root-build-dir',
                      help='Path to root of the build directory', required=True)
  parser.add_argument('--analyzer-packages',
                      help='Path to .packages file for the analyzer_cli package',
                      required=True)
  parser.add_argument('--analyzer-main', help='Path to the analyzer executable',
                      required=True)
  parser.add_argument('--dart', help='Path to the Dart executable',
                      required=True)
  parser.add_argument('--dart-sdk', help='Path to the Dart SDK',
                      required=True)
  parser.add_argument('--package-name', help='Name of the analyzed package',
                      required=True)
  parser.add_argument('--options', help='Path to analysis options')
  args = parser.parse_args()

  analyzer_file = os.path.join(args.root_build_dir, args.out)
  analyzer_path = os.path.dirname(analyzer_file)
  if not os.path.exists(analyzer_path):
    os.makedirs(analyzer_path)

  script_template = string.Template('''#!/bin/sh

echo "Package : $package_name"
$dart \\
  --packages=$analyzer_packages \\
  $analyzer_main \\
  --packages=$dot_packages \\
  --dart-sdk=$dart_sdk \\
  $options_argument \\
  $source_dir \\
  "$$@"
''')
  with open(analyzer_file, 'w') as file:
      file.write(script_template.substitute(
          args.__dict__,
          options_argument = '--options='+args.options if args.options else ''))
  permissions = (stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |
                 stat.S_IRGRP | stat.S_IWGRP | stat.S_IXGRP |
                 stat.S_IROTH)
  os.chmod(analyzer_file, permissions)


if __name__ == '__main__':
  sys.exit(main())
