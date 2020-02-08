#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import re
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    SCRIPT_DIR))                 # unification


class Library(object):

    PARAMS = ['kernel', 'static', 'shared', 'host']

    def __init__(self, name, file):
        self.name = name
        self.file = os.path.relpath(file, os.path.join(FUCHSIA_ROOT, 'zircon'))

    def set(self, param):
        setattr(self, param, True)

    def to_json(self):
        result = {
            'name': self.name,
            'file': self.file,
        }
        for param in Library.PARAMS:
            result[param] = getattr(self, param, False)
        return result

    def fingerprint(self):
        params = [p for p in Library.PARAMS if getattr(self, p, False)]
        return '|'.join(params) if params else 'none'

    def __repr__(self):
        return json.dumps(self.to_json(), indent=2, sort_keys=True,
                          separators=(',', ': '))


class CustomJSONEncoder(json.JSONEncoder):

    def default(self, object):
        if isinstance(object, Library):
            return object.to_json()
        return json.JSONEncoder.default(self, object)


def main():
    parser = argparse.ArgumentParser(description='Analyzes Zircon libraries')
    parser.add_argument('--output',
                        help='file to write library information to',
                        required=False)
    args = parser.parse_args()

    libraries = []
    for base, _, files in os.walk(os.path.join(FUCHSIA_ROOT, 'zircon')):
        for file in files:
            if file != 'BUILD.gn':
                continue
            build_path = os.path.join(base, file)
            with open(build_path, 'r') as build_file:
                lines = build_file.readlines()
            current_library = None
            bracket_count = 0
            for line in lines:
                match = re.match('^\s*zx_library\("([^"]+)"\)\s*{$', line)
                if match:
                    current_library = Library(match.group(1), build_path)
                    bracket_count = 1
                    continue
                if not current_library:
                    continue
                for param in Library.PARAMS:
                    if param + ' = true' in line:
                        current_library.set(param)
                bracket_count += line.count('{')
                bracket_count -= line.count('}')
                if bracket_count <= 0:
                    libraries.append(current_library)
                    current_library = None
                    bracket_count = 0

    if args.output:
        with open(args.output, 'w') as output_file:
            json.dump(libraries, output_file, cls=CustomJSONEncoder, indent=2,
                      sort_keys=True, separators=(',', ': '))

    stats = {}
    for lib in libraries:
        fingerprint = lib.fingerprint()
        stats[fingerprint] = stats.setdefault(fingerprint, 0) + 1
    for stat in sorted(stats.items(), key=lambda item: item[1]):
        print('{:>18}{:>5}'.format(*stat))

    return 0


if __name__ == '__main__':
    sys.exit(main())
