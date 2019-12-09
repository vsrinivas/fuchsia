#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys
import os
import shlex


def main():
    parser = argparse.ArgumentParser(
        description=
        'Create trampolines and a manifest for a set of shell commands',
        fromfile_prefix_chars='@')
    parser.convert_arg_line_to_args = shlex.split
    parser.add_argument(
        '--trampoline-dir',
        required=True,
        help='Directory in which to create trampolines')
    parser.add_argument(
        '--output-manifest', required=True, help='Output manifest path')
    parser.add_argument(
        '--command-list',
        required=True,
        help='A file containing a list of command URI')

    args = parser.parse_args()

    if not os.path.exists(args.trampoline_dir):
        os.makedirs(args.trampoline_dir)

    commands = dict()

    with open(args.command_list) as f:
        uris = f.readlines()

    for uri in uris:
        uri = uri.rstrip()
        name = uri.split('#')[-1]
        name = os.path.split(name)[-1]
        if name in commands:
            sys.stderr.write('Duplicate shell command name: %s\n' % name)
            return 1
        path = os.path.join(args.trampoline_dir, name)
        with open(path, 'w') as f:
            f.write('#!resolve %s\n' % uri)
        commands[name] = path

    with open(args.output_manifest, 'w') as output:
        for name, path in commands.items():
            output.write('bin/%s=%s\n' % (name, path))


if __name__ == '__main__':
    sys.exit(main())
