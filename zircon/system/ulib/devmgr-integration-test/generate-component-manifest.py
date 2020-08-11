#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate a Fuchsia component manifest for isolated devmgr."""
from __future__ import print_function

import os
import argparse
import json
import sys


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--executable-path', required=True, help="Executable path")
    parser.add_argument(
        '--additional-services',
        action='append',
        default=[],
        help='Additional system servies.')
    parser.add_argument(
        '--output', required=True, help='Optional output file path.')
    parser.add_argument(
        '--additional-features',
        action='append',
        default=[],
        help='Additional features.')

    args = parser.parse_args(argv)

    output = open(args.output, 'w')

    content = {
        'facets':
            {
                'fuchsia.test': {
                    'system-services': args.additional_services,
                },
            },
        'program': {
            'binary': args.executable_path
        },
        'sandbox':
            {
                'features':
                    args.additional_features,
                'services':
                    [
                        "fuchsia.exception.Handler",
                        "fuchsia.logger.LogSink",
                        "fuchsia.process.Launcher",
                    ] + args.additional_services,
            },
    }

    json.dump(content, output, indent=4, separators=(',', ': '), sort_keys=True)
    output.close()


if __name__ == "__main__":
    main(sys.argv[1:])
