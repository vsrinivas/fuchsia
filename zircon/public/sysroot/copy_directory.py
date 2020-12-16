#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# # Use of this source code is governed by a BSD-style license that can be
# # found in the LICENSE file.
"""Copy a directory into another one, completely replacing its previous content, if necessary."""

import argparse
import hashlib
import os
import pathlib
import shutil
import sys


def _compute_dir_hash(path):
    """Compute a unique hash corresponding to the content of a given directory."""
    hasher = hashlib.md5()
    for e in sorted(os.scandir(path), key=lambda x: x.name):
        hasher.update(e.name.encode('utf-8') + b'\n')
        if e.is_dir(follow_symlinks=False):
            hasher.update(_compute_dir_hash(e.path))
        elif e.is_file(follow_symlinks=False):
            with open(e.path, 'rb') as f:
                hasher.update(f.read())
        elif e.is_symlink():
            hasher.update(os.readlink(e.path).encode('utf-8'))
        else:
            assert False, 'Not a file, directory or symlink: %s' % e.path
    return hasher.digest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--src', required=True, help='Source directory path.')
    parser.add_argument(
        '--dst', required=True, help='Destination directory path.')

    args = parser.parse_args()

    src_dir = args.src
    assert os.path.exists(src_dir), 'Missing source directory: ' + src_dir
    assert os.path.isdir(src_dir), 'Not a directory: ' + src_dir

    dst_dir = args.dst
    if os.path.exists(dst_dir):
        assert os.path.isdir(dst_dir), (
            'Destination path exists but is not a directory: ' + dst_dir)
        src_hash = _compute_dir_hash(src_dir)
        dst_hash = _compute_dir_hash(dst_dir)
        if dst_hash == src_hash:
            # Don't do anything if both directories have the same content.
            return 0

        shutil.rmtree(dst_dir)

    shutil.copytree(src_dir, dst_dir, symlinks=True)

    # Ensure the destination directory is younger than any file it contains.
    # This later prevent Ninja getting confused, and forcing rebuilds when
    # none are needed :-(
    pathlib.Path(dst_dir).touch()
    return 0


if __name__ == '__main__':
    sys.exit(main())
