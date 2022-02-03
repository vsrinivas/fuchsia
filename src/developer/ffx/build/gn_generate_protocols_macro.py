#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys
from jinja2 import Environment, FileSystemLoader


def main(args_list=None):
    parser = argparse.ArgumentParser(
        description='Generate FFX Services Register Macro')
    parser.add_argument(
        '--template',
        help='The template file to use to generate code',
        required=True)
    parser.add_argument(
        '--out', help='The output file to generate', required=True)
    parser.add_argument(
        '--deps', help='Comma-separated list of protocols', required=True)
    parser.add_argument(
        '--deps_full',
        help='Comma-separated list of protocol labels',
        required=True)
    if args_list:
        args = parser.parse_args(args_list)
    else:
        args = parser.parse_args()

    # Zip together deps with their full path.
    deps = zip(args.deps.split(","), args.deps_full.split(","))
    deps = list(map(lambda i: {'lib': i[0], 'target': i[1]}, deps))

    template_dir, template_name = os.path.split(args.template)
    env = Environment(
        loader=FileSystemLoader(template_dir),
        trim_blocks=True,
        lstrip_blocks=True)
    template = env.get_template(template_name)
    with open(args.out, 'w') as f:
        render = template.render(deps=deps)
        f.write(render)


if __name__ == '__main__':
    sys.exit(main())
