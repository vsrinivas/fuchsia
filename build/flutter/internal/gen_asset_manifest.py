#!/usr/bin/env python3.8
# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import subprocess
import sys


def fini_to_json(input, output):
    with open(input, 'r') as input_file:
        lines = input_file.read().splitlines()

    entries = []
    for line in lines:
        values = line.split("=", 1)
        entries.append({'source': values[1], 'destination': values[0]})

    with open(output, 'w') as output_file:
        json.dump(
            sorted(entries, key=lambda x: (x['destination'], x['source'])),
            output_file,
            indent=2,
            sort_keys=True,
            separators=(',', ': '))


def rewrite_depfile(old_depfile, new_depfile, output):
    content = ''
    with open(old_depfile, 'r') as f:
        content = f.read()
    _, sep, ins = content.strip().partition(':')
    if not set:
        raise ValueError('depfile has incorrect format, no ":" found')
    relative_ins = (os.path.relpath(p) for p in ins.split())
    # TODO(https://fxbug.dev/78719): Figure out why the following yamls are
    # considered dependencies and remove this hardcoded filter. Currently we are
    # only seeing this from flutter_apps used in tests, which is allowlisted, so
    # we don't expect this list to grow.
    non_existant_pubspecs = {
        '../../src/ui/tests/integration_flutter_tests/embedder/pubspec.yaml',
        '../../src/ui/tests/integration_input_tests/touch/pubspec.yaml',
    }
    filtered_relative_ins = ' '.join(
        p for p in relative_ins if p not in non_existant_pubspecs)
    with open(new_depfile, 'w') as f:
        f.write(f'{output}: {filtered_relative_ins}\n')


def main():
    parser = argparse.ArgumentParser(description='Create an asset manifest')

    parser.add_argument(
        '--flutter-root',
        type=str,
        required=True,
        help='The root of the Flutter SDK')
    parser.add_argument(
        '--flutter-tools',
        type=str,
        required=True,
        help='The executable for the Flutter tool')
    parser.add_argument(
        '--asset-dir',
        type=str,
        required=True,
        help='The directory where to put intermediate files')
    parser.add_argument(
        '--packages', type=str, required=True, help='The package map to use')
    parser.add_argument(
        '--manifest', type=str, help='The application manifest', required=True)
    parser.add_argument(
        '--component-name',
        type=str,
        help='The name of the component',
        required=True)
    parser.add_argument(
        '--output',
        type=str,
        help=
        'Output path for the asset manifest used by the fuchsia packaging tool')
    parser.add_argument(
        '--depfile', type=str, required=True, help='Path to write depfile')

    args = parser.parse_args()

    env = os.environ.copy()
    env['FLUTTER_ROOT'] = args.flutter_root
    # This ensures the tool does not make unneeded network calls.
    env['BOT'] = 'true'

    intermediate_output = args.output + '.partial'
    intermediate_depfile = args.depfile + '.partial'

    call_args = [
        args.flutter_tools,
        '--asset-dir=%s' % args.asset_dir,
        '--packages=%s' % args.packages,
        '--manifest=%s' % args.manifest,
        '--asset-manifest-out=%s' % intermediate_output,
        '--component-name=%s' % args.component_name,
        '--depfile=%s' % intermediate_depfile
    ]

    result = subprocess.call(call_args, env=env)

    if result == 0:
        fini_to_json(intermediate_output, args.output)
        os.remove(intermediate_output)
        # This script rewrites output to a different file, so we need to rewrite
        # depfile as well.
        rewrite_depfile(intermediate_depfile, args.depfile, args.output)
        os.remove(intermediate_depfile)

    return result


if __name__ == '__main__':
    sys.exit(main())
