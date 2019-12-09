#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser('Generate a sysmgr config to persist logs')
    parser.add_argument('name', help='Name of the config')
    parser.add_argument('path', help='Path to the package file')
    parser.add_argument('--tags', help='Tag to filter for', nargs='+')
    parser.add_argument('--ignore-tags', help='Tag to ignore', nargs='+')
    parser.add_argument(
        '--file-capacity', help='max allowed disk usage', type=int)
    parser.add_argument(
        '--startup-sleep', help='time to sleep on startup', type=int)
    args = parser.parse_args()

    tag_args = []
    if args.tags:
        for t in args.tags:
            tag_args += ['--tag', t]

    ignore_tag_args = []
    if args.ignore_tags:
        for t in args.ignore_tags:
            ignore_tag_args += ['--ignore-tag', t]

    file_cap_args = []
    if args.file_capacity != None:
        file_cap_args = ['--file_capacity', str(args.file_capacity)]

    startup_sleep_args = []
    if args.startup_sleep != None:
        file_cap_args = ['--startup_sleep', str(args.startup_sleep)]

    with open(args.path, 'w') as f:
        json.dump(
            {
                'apps':
                    [
                        [
                            "fuchsia-pkg://fuchsia.com/log_listener#meta/log_listener.cmx",
                            "--file", "/cache/logs." + args.name
                        ] + file_cap_args + tag_args + ignore_tag_args +
                        startup_sleep_args
                    ]
            }, f)

    return 0


if __name__ == '__main__':
    sys.exit(main())
