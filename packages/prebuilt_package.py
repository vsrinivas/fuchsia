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
    parser.add_argument('--merkleroot-tool', help='Path to merkleroot tool')
    parser.add_argument('--name', help='Name of prebuilt package')
    parser.add_argument('--archive', help='Path to archive containing prebuilt package')
    parser.add_argument('--workdir', help='Path to working directory')
    parser.add_argument('--manifest', help='Manifest file to generate')
    parser.add_argument('--blobs-rsp', help='Blobs response file to generate')
    parser.add_argument('--system-rsp', help='System response file to generate')
    parser.add_argument('--ids-txt', help='ids.txt file to generate')
    parser.add_argument('--blobs-json', help='blobs.json file to generate')
    parser.add_argument('--meta-merkle', help='meta.far.merkle file to generate')

    args = parser.parse_args()

    extract(args.pm_tool, args.archive, args.workdir)

    meta_far = os.path.join(args.workdir, 'meta.far')

    merkle = subprocess.check_output([args.merkleroot_tool, meta_far]).split()[0]

    bloblist = findblobs(args.workdir)

    with open(args.meta_merkle, 'w') as f:
        f.write(merkle)

    with open(args.manifest, 'w') as f:
        for blob in bloblist:
            f.write('%s=%s/blobs/%s\n' % (blob['name'], args.workdir,
                blob['name']))

    with open(args.system_rsp, 'w') as f:
        f.truncate(0)

    with open(args.ids_txt, 'w') as f:
        f.truncate(0)

    with open(args.blobs_json, 'w') as f:
        blobs = []
        blobs.append({"source_path": meta_far,
                      "path": "meta/",
                      "merkle": merkle,
                      "size": os.path.getsize(meta_far)})
        for blob in bloblist:
            source_path = blob['path']
            path = ''
            merkle = blob['name']
            size = blob['size']
            blobs.append({"source_path": source_path,
                          "path": path,
                          "merkle": merkle,
                          "size": size})
        json.dump(blobs, f)

    with open(args.blobs_rsp, 'w') as f:
        f.write('--input=%s\n' % args.blobs_json)

    return 0


if __name__ == '__main__':
    sys.exit(main())
