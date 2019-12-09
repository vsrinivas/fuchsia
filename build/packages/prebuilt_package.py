#!/usr/bin/env python2.7
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
    parser.add_argument('--system-rsp', help='System response file to generate')

    args = parser.parse_args()

    extract(args.pm_tool, args.archive, args.workdir)

    with open(args.system_rsp, 'w') as f:
        f.truncate(0)

    return 0


if __name__ == '__main__':
    sys.exit(main())
