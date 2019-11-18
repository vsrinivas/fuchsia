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
    if args.type in ('test'):
        binary_path = 'test/'
    else:
        binary_path = 'bin/'

    content = {
        'program': {
            'binary': binary_path + args.name
        },
        'sandbox':
            {
                'features': ['isolated-temp',],
                'services': ['fuchsia.logger.LogSink',],
            },
    }

    # Our Vulkan test programs needs specific features and
    # services. Note that 'isolated-cache-storage' is required for
    # VkPipelineCache uses.
    if needs_vulkan:
        content['sandbox']['features'] += [
            'isolated-cache-storage',
            'vulkan',
        ]
        content['sandbox']['services'] += [
            'fuchsia.sysmem.Allocator',
            'fuchsia.tracing.provider.Registry',
            'fuchsia.vulkan.loader.Loader',
        ]

    # Accessing the framebuffer requires this device driver
    if args.needs_vulkan_framebuffer:
        content['sandbox']['dev'] = [
            'class/display-controller',
        ]

    # Inject Vulkan-related services into test component manifest.
    if args.type in ('test') and needs_vulkan:
        content['facets'] = {
            'fuchsia.test':
                {
                    'injected-services':
                        {
                            "fuchsia.tracing.provider.Register":
                                "fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx",
                        },
                    'system-services':
                        [
                            "fuchsia.sysmem.Allocator",
                            "fuchsia.vulkan.loader.Loader",
                        ],
                },
        }

    json.dump(content, output, indent=4, separators=(',', ': '), sort_keys=True)
    output.close()


if __name__ == "__main__":
    main(sys.argv[1:])
