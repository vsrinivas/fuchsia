#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser("Create a manifest for a seed corpus")
    parser.add_argument("--corpus", help="Path to corpus directory")
    parser.add_argument("--depfile", help="Path to depfile", required=True)
    parser.add_argument("--fuzzer", help="Name of fuzzer", required=True)
    parser.add_argument("--manifest", help="Output manifest", required=True)
    args = parser.parse_args()

    # Enumerate the corpus as a list of (srcpath, dstpath) pairs
    elements = sorted(os.listdir(args.corpus)) if args.corpus else []
    elements = [
        (
            os.path.join(args.corpus, element),
            os.path.join('data', args.fuzzer, 'corpus', element))
        for element in elements
    ]

    # The manifest will be included in the package manifest to include corpus
    # elements as resources.
    with open(args.manifest, 'wb') as manifest:
        for srcpath, dstpath in elements:
            manifest.write('{}={}\n'.format(dstpath, srcpath).encode())

    # The depfile is used by GN to trigger a re-packaging when the corpus
    # changes.
    with open(args.depfile, 'wb') as depfile:
        depfile.write((args.manifest + ':').encode())
        for srcpath, dstpath in elements:
            depfile.write((' ' + os.path.abspath(srcpath)).encode())
        depfile.write(b'\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
