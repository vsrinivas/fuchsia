#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import re
import sys

from common import FUCHSIA_ROOT


class Sdk(object):

    SOURCE = 'source'
    STATIC = 'static'
    SHARED = 'shared'
    NOPE = ''


class Library(object):

    FINGERPRINT_PARAMS = ['kernel', 'static', 'shared', 'host']
    PARAMS = FINGERPRINT_PARAMS + [ 'sdk_publishable' ]

    def __init__(self, name, file):
        self.name = name
        self.file = os.path.relpath(file, os.path.join(FUCHSIA_ROOT, 'zircon'))
        self.sdk = Sdk.NOPE
        for param in Library.PARAMS:
            setattr(self, param, False)

    def set(self, param):
        setattr(self, param, True)

    def set_sdk(self, value):
        self.sdk = value

    def to_json(self):
        result = {
            'name': self.name,
            'file': self.file,
        }
        for param in Library.PARAMS + ['sdk']:
            result[param] = getattr(self, param, False)
        return result

    def fingerprint(self):
        params = [p for p in Library.FINGERPRINT_PARAMS if getattr(self, p, False)]
        return '|'.join(params) if params else 'none'

    def __repr__(self):
        return json.dumps(self.to_json(), indent=2, sort_keys=True,
                          separators=(',', ': '))


def get_library_stats(build_path):
    result = []
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
        match = re.match('^\s*sdk = "([^"]+)"\s*$', line)
        if match:
            current_library.set_sdk(match.group(1))
        bracket_count += line.count('{')
        bracket_count -= line.count('}')
        if bracket_count <= 0:
            result.append(current_library)
            current_library = None
            bracket_count = 0
    return result


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
            libraries.extend(get_library_stats(build_path))

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
