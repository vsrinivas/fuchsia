#!/usr/bin/env python2.7
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
    parser.add_argument('--input', help='Input JSON file', required=True)
    parser.add_argument('--output', help='Output JSON file', required=True)
    parser.add_argument(
        '--manifest', help='Output manifest file', required=True)
    parser.add_argument('--location', help='JSON pointer', required=True)
    args = parser.parse_args()

    # Read in the original JSON tree.
    with open(args.input) as f:
        data = json.load(f)

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
    with open(args.manifest, 'w') as f:
        for dest, source in manifest.iteritems():
            f.write('%s=%s\n' % (dest, source))

    # Write out the modified JSON tree.
    with open(args.output, 'w') as f:
        json.dump(data, f, indent=2, sort_keys=True)

    return 0


if __name__ == '__main__':
    sys.exit(main())
