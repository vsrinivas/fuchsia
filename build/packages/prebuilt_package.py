#!/usr/bin/env python
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
    args = [pm, 'expand', far_path]
    subprocess.check_output(args, stderr=subprocess.STDOUT, cwd=workdir)


def findblobs(workdir):
    bloblist = []
    srcdir = os.path.join(workdir, 'blobs')
    for blob in os.listdir(srcdir):
        path = os.path.join(srcdir, blob)
        bloblist.append({'name': blob,
                         'path': path,
                         'size': os.stat(path).st_size})
    return bloblist


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pm-tool', help='Path to pm tool')
    parser.add_argument('--name', help='Name of prebuilt package')
    parser.add_argument('--archive', help='Path to archive containing prebuilt package')
    parser.add_argument('--workdir', help='Path to working directory')
    parser.add_argument('--manifest', help='Manifest file to generate')
    parser.add_argument('--system-rsp', help='System response file to generate')
    parser.add_argument('--ids-txt', help='ids.txt file to generate')

    args = parser.parse_args()

    extract(args.pm_tool, args.archive, args.workdir)

    bloblist = findblobs(args.workdir)

    with open(args.manifest, 'w') as f:
        for blob in bloblist:
            f.write('%s=%s/blobs/%s\n' % (blob['name'], args.workdir,
                blob['name']))

    with open(args.system_rsp, 'w') as f:
        f.truncate(0)

    with open(args.ids_txt, 'w') as f:
        f.truncate(0)

    return 0


if __name__ == '__main__':
    sys.exit(main())
