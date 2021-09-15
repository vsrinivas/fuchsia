#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
import argparse
import filecmp
import os
import shutil
import string
import sys
import tempfile

from jinja2 import Environment, FileSystemLoader


def to_camel_case(snake_str):
    components = snake_str.split('_')
    return ''.join(x.title() for x in components[0:])


def wrap_deps(dep):
    return {'enum': to_camel_case(dep), 'lib': dep + '_args'}


def main(args_list=None):
    parser = argparse.ArgumentParser(description='Generate FFX Command struct')

    parser.add_argument(
        '--out', help='The output file to generate', required=True)

    parser.add_argument(
        '--deps',
        help='Comma-seperated libraries to generate code from',
        required=True)

    parser.add_argument(
        '--template',
        help='The template file to use to generate code',
        required=True)

    if args_list:
        args = parser.parse_args(args_list)
    else:
        args = parser.parse_args()

    template_dir, template_name = os.path.split(args.template)
    env = Environment(
        loader=FileSystemLoader(template_dir),
        trim_blocks=True,
        lstrip_blocks=True)
    template = env.get_template(template_name)
    libraries = args.deps.split(',')
    deps = map(wrap_deps, libraries)
    temp_file = tempfile.NamedTemporaryFile(mode='w')
    with open(temp_file.name, 'w') as file:
        file.write(template.render(deps=deps))
        file.flush()
        if (not os.path.isfile(args.out) or not
            filecmp.cmp(temp_file.name, args.out, shallow=False)):
            shutil.copyfile(temp_file.name, args.out)


if __name__ == '__main__':
    sys.exit(main())
