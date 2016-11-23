#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import paths
import subprocess
import sys


def gn_describe(out, path):
  gn = os.path.join(paths.FUCHSIA_ROOT, 'packages', 'gn', 'gn.py')
  data = subprocess.check_output([gn, 'desc', out, path, '--format=json'])
  return json.loads(data)


def main():
  parser = argparse.ArgumentParser('Run Dart analysis for Dart build targets')
  parser.add_argument('--out',
                      help='Path to the base output directory, e.g. out/debug-x86-64',
                      required=True)
  parser.add_argument('--tree',
                      help='Restrict analysis to a source subtree, e.g. //apps/sysui/*',
                      default='*')
  args = parser.parse_args()

  scripts = []
  targets = gn_describe(args.out, args.tree)
  for target_name, properties in targets.items():
    if ('type' not in properties or
        properties['type'] != 'action' or
        'script' not in properties or
        properties['script'] != '//build/dart/gen_analyzer_invocation.py' or
        'outputs' not in properties or
        not len(properties['outputs'])):
      continue
    script_path = properties['outputs'][0]
    script_path = script_path[2:]  # Remove the leading //
    scripts.append(os.path.join(paths.FUCHSIA_ROOT, script_path))

  has_errors = False
  for script in scripts:
    print '----------------------------------------------------------'
    has_errors |= subprocess.call([script], stdout=sys.stdout, stderr=sys.stderr) != 0
    print ''

  if has_errors:
    print 'Analysis failed, see issues above'
    exit(1)


if __name__ == '__main__':
  sys.exit(main())
