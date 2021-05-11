#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string
import sys

# Root dir is 5 levels up from here.
FUCHSIA_DIR = os.path.abspath(
    os.path.join(
        __file__, os.pardir, os.pardir, os.pardir, os.pardir, os.pardir))
sys.path += [os.path.join(FUCHSIA_DIR, 'third_party')]
from jinja2 import Environment, FileSystemLoader


def main(args_list=None):
    parser = argparse.ArgumentParser(
        description='Generate FFX Services Register Macro')
    parser.add_argument(
        '--out', help='The output file to generate', required=True)
    parser.add_argument(
        '--deps', help='Comma-separated list of services', required=True)
    parser.add_argument(
        '--deps_full',
        help='Comma-separated list of service labels',
        required=True)
    if args_list:
        args = parser.parse_args(args_list)
    else:
        args = parser.parse_args()

    # Zip together deps with their full path.
    deps = zip(args.deps.split(","), args.deps_full.split(","))
    deps = list(map(lambda i: {'lib': i[0], 'target': i[1]}, deps))
    template_path = os.path.join(os.path.dirname(__file__), 'templates')
    env = Environment(
        loader=FileSystemLoader(template_path),
        trim_blocks=True,
        lstrip_blocks=True)
    template = env.get_template('services_macro.md')
    with open(args.out, 'w') as f:
        render = template.render(deps=deps)
        f.write(render)


if __name__ == '__main__':
    sys.exit(main())
