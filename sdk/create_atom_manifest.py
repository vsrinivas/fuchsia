#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


class Atom(object):
    '''Wrapper class for atom data, adding convenience methods.'''

    def __init__(self, json):
        self.json = json
        self.name = json['name']

    def __str__(self):
        return self.name

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        return self.name == other.name

    def __ne__(self, other):
        return not __eq__(self, other)


def gather_dependencies(manifests):
    '''Extracts all required atoms from the given manifests, as well as the
       names of all the direct dependencies.
       '''
    direct_deps = []
    atoms = set()
    for dep in manifests:
        with open(dep, 'r') as dep_file:
            dep_manifest = json.load(dep_file)
            if dep_manifest['type'] == 'atom':
                direct_deps.append(dep_manifest['name'])
            atoms.update(map(lambda a: Atom(a), dep_manifest['atoms']))
    return (direct_deps, atoms)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name',
                        help='Name of the element',
                        required=True)
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--base',
                        help='Path to the element\'s source directory',
                        required=True)
    parser.add_argument('--deps',
                        help='List of manifest paths for dependencies',
                        nargs='*')
    parser.add_argument('--files',
                        help='A source=destination mapping',
                        nargs='*')
    parser.add_argument('--file-manifest',
                        help='A file containing source=destination mappings',
                        required=False)
    parser.add_argument('--tags',
                        help='List of tags for the included elements',
                        nargs='*')
    args = parser.parse_args()

    # Gather the definitions of other atoms this atom depends on.
    (deps, atoms) = gather_dependencies(args.deps)

    # Build the list of files making up this atom.
    files = {}
    base = os.path.realpath(args.base)
    mappings = args.files
    if args.file_manifest:
        with open(args.file_manifest, 'r') as manifest_file:
            additional_mappings = [l.strip() for l in manifest_file.readlines()]
            mappings.extend(additional_mappings)
    for mapping in mappings:
        destination, source = mapping.split('=', 1)
        real_source = os.path.realpath(source)
        if not os.path.exists(real_source):
            raise Exception('Missing source file: %s' % real_source)
        if not destination:
            if not real_source.startswith(base):
                raise Exception('Destination for %s must be given as it is not'
                                ' under source directory %s' % (source, base))
            destination = os.path.relpath(real_source, base)
        if os.path.isabs(destination):
            raise Exception('Destination cannot be absolute: %s' % destination)
        files[destination] = real_source

    atoms.update([Atom({
        'name': args.name,
        'tags': args.tags,
        'deps': deps,
        'files': files,
    })])
    manifest = {
        'type': 'atom',
        'name': args.name,
        'atoms': map(lambda a: a.json, list(atoms)),
    }

    with open(os.path.abspath(args.out), 'w') as out:
        json.dump(manifest, out, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
