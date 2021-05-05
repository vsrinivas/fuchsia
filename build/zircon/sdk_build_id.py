#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

sys.path.append(os.path.join(
    os.path.dirname(__file__),
    os.pardir,
    "cpp",
))
import binaries


# Rewrite a debug file name (or list of them) into its .build-id/...
# name for publication, collecting the mappings in the `manifest` dict.
def rewrite(debug, manifest):
    if isinstance(debug, list):
        return [rewrite(x, manifest) for x in debug]
    id_path = binaries.get_sdk_debug_path(debug)
    manifest[id_path] = debug
    return id_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--input',
        type=argparse.FileType('r'),
        help='Input JSON file',
        required=True,
    )
    parser.add_argument(
        '--output',
        type=argparse.FileType('w'),
        help='Output JSON file',
        required=True,
    )
    parser.add_argument(
        '--manifest',
        type=argparse.FileType('w'),
        help='Output manifest file',
        required=True,
    )
    parser.add_argument(
        '--depfile',
        type=argparse.FileType('w'),
        required=True,
    )
    parser.add_argument('--location', help='JSON pointer', required=True)
    args = parser.parse_args()

    # Read in the original JSON tree.
    data = json.load(args.input)

    # Poor man's JSON pointer: /foo/bar/baz looks up in dicts.
    ptr = args.location.split('/')
    assert ptr[0] == '', '%s should be absolute JSON pointer' % args.location

    # Follow the pointer steps until the last one, so walk[ptr[0]] points
    # at the requested location in the JSON tree.
    walk = {'': data}
    while len(ptr) > 1:
        walk = walk[ptr[0]]
        ptr = ptr[1:]

    # Apply the rewrites to the string or list at that spot in the tree.
    manifest = {}
    walk[ptr[0]] = rewrite(walk[ptr[0]], manifest)

    # Write out the manifest collected while rewriting original debug files
    # names to .build-id/... names for publication.
    #
    # Original debug files are read during the rewrite above, so include them in
    # a depfile.
    mappings = []
    deps = []
    for dest, source in manifest.items():
        mappings.append(f'{dest}={source}')
        deps.append(os.path.relpath(source))
    args.manifest.write('\n'.join(mappings))
    args.depfile.write(
        '{} {}: {}\n'.format(
            args.manifest.name, args.output.name, ' '.join(deps)))

    # Write out the modified JSON tree.
    json.dump(data, args.output, indent=2, sort_keys=True)

    return 0


if __name__ == '__main__':
    sys.exit(main())
