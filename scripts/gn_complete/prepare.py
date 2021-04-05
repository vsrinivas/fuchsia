#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''
Pre-processes the project.json file emitted by GN to only include target names.

This should be run as part of `fx set` with the flags
`--ide=json --json-ide-script=//scripts/prepare_targets.py`.

This generates a file `//out/default/project_lite.json` which contains a list of
target names available in the build. This file is read by `complete.py` to
provide completions for `fx build`.

Pre-processing the `project.json` file emitted by GN is crucial for completion
performance.
'''

import argparse
import json
import os
import sys

def eprint(*args, **kwargs):
  print(*args, file=sys.stderr, **kwargs)

FUCHSIA_DIR = os.environ['FUCHSIA_DIR']
DEFAULT_OUT_NAME = 'project_lite.json'

try:
  DEFAULT_OUT_DIR = os.environ['FUCHSIA_BUILD_DIR']
except KeyError:
  DEFAULT_OUT_DIR = os.path.join(FUCHSIA_DIR, 'out', 'default')


def main(args):
  document_lite = {}
  with open(args.project_file) as fin:
    document = json.load(fin)
    document_lite['targets'] = list(document['targets'].keys())
    project_file = os.path.join(args.build_dir, args.out_name)
    with open(project_file, 'w+') as fout:
      json.dump(document_lite, fout)
    print('Wrote JSON targets file for shell completion: {}'.format(
        project_file))


if __name__ == '__main__':
  p = argparse.ArgumentParser(description=__doc__)
  p.add_argument('--build_dir', default=DEFAULT_OUT_DIR)
  p.add_argument('--out_name', default=DEFAULT_OUT_NAME)
  p.add_argument('project_file')
  try:
    main(p.parse_args())
  except Exception as e:
    eprint('error: {}'.format(e))
    sys.exit(1)
