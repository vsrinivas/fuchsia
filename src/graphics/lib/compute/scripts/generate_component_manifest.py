#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate a Fuchsia component manifest for graphics compute targets."""
from __future__ import print_function

import os
import argparse
import json
import sys

_COMPONENT_TYPES = [
    'executable',
    'test',
]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True, help="Component name")
    parser.add_argument(
        '--type',
        default='executable',
        choices=_COMPONENT_TYPES,
        help='Component type')
    parser.add_argument(
        '--needs-vulkan', action='store_true', help='Component needs Vulkan.')
    parser.add_argument(
        '--needs-vulkan-framebuffer',
        action='store_true',
        help='Component needs Vulkan and framebuffer access.')
    parser.add_argument('--output', help='Optional output file path.')

    args = parser.parse_args(argv)

    if not args.output:
        output = sys.stdout
    else:
        output = open(args.output, 'w')

    needs_vulkan = args.needs_vulkan or args.needs_vulkan_framebuffer

    # Output is a JSON dictionary.
    content = {
        'include': ["syslog/client.shard.cml"],
        'program': {
            'binary': "bin/" + args.name
        },
    }

    # Is this a gtest?
    if args.type in ('test'):
        content['include'] += [
            "//src/sys/test_runners/gtest/default.shard.cml",
            "sys/testing/system-test.shard.cml",
        ]

    # Is this an executable?
    if args.type in ('executable'):
        content['program']['runner'] = "elf"
        content['program']['forward_stdout_to'] = "log"
        content['program']['forward_stderr_to'] = "log"

    # A Vulkan executable typically reads/writes a pipeline cache.
    if needs_vulkan:
        content['include'] += [
            "vulkan/client.shard.cml",
        ]
        content['use'] = [
            {
                'storage': "cache",
                'path': "/cache",
                'rights': ["rw*"],
            },
        ]

        # Is this Vulkan and an executable?
        if args.type in ('executable'):
            content['use'] += [
                {
                    'storage': "tmp",
                    'path': "/tmp",
                    'rights': ["w*"],
                },
                {
                    'directory': "dev-display-controller",
                    'path': "/dev/class/display-controller",
                    'rights': ["rw*"],
                },
                {
                    'directory': "dev-input-report",
                    'path': "/dev/class/input-report",
                    'rights': ["rw*"],
                },
            ]

    json.dump(content, output, indent=4, separators=(',', ': '), sort_keys=True)
    output.close()


if __name__ == "__main__":
    main(sys.argv[1:])
