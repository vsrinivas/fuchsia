#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import zipfile
import subprocess
import os
import shutil


def zip_dir(dir, zip_file):
    for root, _dirs, files in os.walk(dir):
        for file in files:
            path = os.path.join(root, file)
            zip_file.write(path, os.path.relpath(path, dir))


def prepare_dirs(pm_dir):
    dirs = {'pm': pm_dir}
    os.makedirs(dirs['pm'], exist_ok=True)
    for dir in ['keys', 'repository']:
        dirs[dir] = '{}/{}'.format(pm_dir, dir)
        os.makedirs(dirs[dir], exist_ok=True)
    return dirs


# `pm publish` expects the following inputs in its in/out directory:
# - `keys/{snapshot|targets|timestamp}.json` containing private metadata keys;
# - `repository/{{verion-num}}.root.json` containing versioned root metadata;
# - `repository/root.json` containing default root metadata.
def pm_prepare_publish(args, dirs):
    for key_file_path in args.key:
        shutil.copy(key_file_path, dirs['keys'])
    for root_metadata_path in args.root_metadata:
        shutil.copy(root_metadata_path, dirs['repository'])
    shutil.copy(
        args.default_root_metadata,
        '{}/{}'.format(dirs['repository'], 'root.json'))


def pm_publish(args, dirs):
    cmd_args = [
        args.pm,
        'publish',
        '-C',
        '-r',
        dirs['pm'],
        '-lp',
        '-f',
        args.input,
        '-vt',
    ]
    subprocess.run(cmd_args, check=True)


def main(args):
    pm_dir = args.gendir
    dirs = prepare_dirs(pm_dir)

    # Prepare for `pm publish` and gather deps associated with preparations.
    pm_prepare_publish(args, dirs)

    # Invoke `pm publish` and gather deps associated with invocation.
    pm_publish(args, dirs)

    # Output repository directory to zip file.
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as zip_file:
        zip_dir(dirs['repository'], zip_file)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        'Creates a zip archive of the TUF repository output by `pm publish`')
    parser.add_argument('--pm', help='path to the pm executable', required=True)
    parser.add_argument(
        '--key',
        help='path to a key file to be consumed by `pm publish`',
        action='append')
    parser.add_argument(
        '--root-metadata',
        help='path to a root metadata file to be used in the TUF repository',
        action='append')
    parser.add_argument(
        '--default-root-metadata',
        help='path to the default TUF root metadata file',
        required=True)
    parser.add_argument('--input', help='path to pm file input', required=True)
    parser.add_argument(
        '--gendir',
        help=
        'path to the directory where artifacts are gathered before being zipped',
        required=True)
    parser.add_argument('--output', help='path output zip file', required=True)
    main(parser.parse_args())
