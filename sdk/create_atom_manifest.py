#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import json
import os
import sys


class AtomId(object):
    '''Represents an atom id.'''

    def __init__(self, json):
     self.json = json
     self.key = (json['domain'], json['name'])

    def __str__(self):
        return '%s{%s}' % (self.json['name'], self.json['domain'])

    def __hash__(self):
        return hash(self.key)

    def __eq__(self, other):
        return self.key == other.key

    def __ne__(self, other):
        return not __eq__(self, other)

    def __cmp__(self, other):
        return cmp(self.key, other.key)


class Atom(object):
    '''Wrapper class for atom data, adding convenience methods.'''

    def __init__(self, json):
        self.json = json
        self.id = AtomId(json['id'])
        self.label = json['gn-label']

    def __str__(self):
        return str(self.id)

    def __hash__(self):
        return hash(self.label)

    def __eq__(self, other):
        return self.label == other.label

    def __ne__(self, other):
        return not __eq__(self, other)

    def __cmp__(self, other):
        return cmp(self.id, other.id)


def gather_dependencies(manifests):
    '''Extracts the set of all required atoms from the given manifests, as well
       as the set of names of all the direct dependencies.
       '''
    direct_deps = set()
    atoms = set()
    for dep in manifests:
        with open(dep, 'r') as dep_file:
            dep_manifest = json.load(dep_file)
            direct_deps.update(map(lambda i: AtomId(i), dep_manifest['ids']))
            atoms.update(map(lambda a: Atom(a), dep_manifest['atoms']))
    return (direct_deps, atoms)


def detect_collisions(atoms):
    '''Detects name collisions in a given atom list.'''
    mappings = collections.defaultdict(lambda: [])
    for atom in atoms:
        mappings[atom.id].append(atom)
    has_collisions = False
    for id, group in mappings.iteritems():
        if len(group) == 1:
            continue
        has_collisions = True
        labels = [a.label for a in group]
        print('Targets sharing the SDK id %s:' % id)
        for label in labels:
            print(' - %s' % label)
    return has_collisions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--domain',
                        help='Name of the domain the element belongs to',
                        required=True)
    name_group = parser.add_mutually_exclusive_group(required=True)
    name_group.add_argument('--name',
                            help='Name of the element')
    name_group.add_argument('--name-file',
                            help='Path to the file containing the name of the element')
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
    parser.add_argument('--gn-label',
                        help='GN label of the atom',
                        required=True)
    args = parser.parse_args()

    if (args.name):
	name = args.name
    else:
        with open(args.name_file, 'r') as name_file:
            name = name_file.read()

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

    id = {
        'domain': args.domain,
        'name': name,
    }
    tags = dict(map(lambda t: t.split(':', 1), args.tags))
    atoms.update([Atom({
        'id': id,
        'gn-label': args.gn_label,
        'tags': tags,
        'deps': map(lambda i: i.json, sorted(list(deps))),
        'files': files,
    })])
    if detect_collisions(atoms):
        print('Name collisions detected!')
        return 1

    manifest = {
        'ids': [id],
        'atoms': map(lambda a: a.json, sorted(list(atoms))),
    }

    with open(os.path.abspath(args.out), 'w') as out:
        json.dump(manifest, out, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
