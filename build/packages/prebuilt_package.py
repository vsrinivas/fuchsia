#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import shutil
import subprocess
import sys


def extract(pm, far_path, workdir):
    if not os.path.exists(workdir):
        os.makedirs(workdir)
    args = [pm, '-o', workdir, 'expand', far_path]
    subprocess.check_output(args, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pm-tool', help='Path to pm tool')
    parser.add_argument('--name', help='Name of prebuilt package')
    parser.add_argument(
        '--archive', help='Path to archive containing prebuilt package')
    parser.add_argument('--workdir', help='Path to working directory')

    args = parser.parse_args()

    extract(args.pm_tool, args.archive, args.workdir)

    with open(os.path.join(args.workdir, 'package_manifest.json')) as f:
        manifest = json.load(f)
        if manifest.get('package').get('name') != args.name:
            sys.stderr.write(
                'Prebuilt package name {} does not match the name contained within {}\n'
                .format(args.name, args.archive))
            return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
