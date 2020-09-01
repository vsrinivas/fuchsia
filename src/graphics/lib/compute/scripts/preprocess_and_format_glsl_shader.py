#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Preprocess and clang-format a single GLSL shader script."""

import argparse
import os
import subprocess
import sys

def _Panic(msg):
    sys.stderr.write('ERROR: %s\n' % msg)
    sys.exit(1)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        '--glslangValidator-tool',
        default='glslangValidator',
        help='Use specific glslangValidator tool path.')

    parser.add_argument('--clang-format-tool',
                        default='clang-format',
                        help='Use specific clang-format tool path.')

    parser.add_argument(
        '-I',
        '--include-dir',
        action='append',
        default=[],
        help='Include directory used during pre-processing')

    parser.add_argument(
        '-o', '--output', default='-', help='Output path, use - for stdout.')

    parser.add_argument('input', help="Input GLSL shader script path.")

    args = parser.parse_args()

    cmd_args = [args.glslangValidator_tool, '-E', args.input]

    # For some reason, this is defined when compiling, but not pre-processing!
    cmd_args += [ '-DVULKAN' ]

    for include_dir in args.include_dir:
        cmd_args.append('-I%s' % include_dir)
    try:
        preprocessed_script = subprocess.check_output(cmd_args)
    except subprocess.CalledProcessError as e:
        _Panic('Preprocessing error: %s' % e)

    try:
        p = subprocess.Popen([args.clang_format_tool],
                             stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE)
        formatted_script, _ = p.communicate(preprocessed_script)
    except subprocess.CalledProcessError as e:
        _Panic('Formatting error: %s' % e)

    out = sys.stdout if args.output == '-' else open(args.output, 'wb')
    out.write(formatted_script)
    out.close()

    sys.exit(0)


if __name__ == "__main__":
    main(sys.argv)
