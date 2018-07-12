# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import json


class AtomId(object):
    '''Represents an atom id.'''

    def __init__(self, json):
     self.json = json
     self.domain = json['domain']
     self.name = json['name']
     self.key = (self.domain, self.name)

    def __str__(self):
        return '%s{%s}' % (self.name, self.domain)

    def __hash__(self):
        return hash(self.key)

    def __eq__(self, other):
        return self.key == other.key

    def __ne__(self, other):
        return not __eq__(self, other)

    def __cmp__(self, other):
        return cmp(self.key, other.key)


class File(object):
    '''Wrapper class for file definitions.'''

    def __init__(self, json):
        self.source = json['source']
        self.destination = json['destination']
        self.is_packaged = json['packaged']

    def __str__(self):
        return '{%s <-- %s}' % (self.destination, self.source)


class Atom(object):
    '''Wrapper class for atom data, adding convenience methods.'''

    def __init__(self, json):
        self.json = json
        self.id = AtomId(json['id'])
        self.label = json['gn-label']
        self.category = json['category']
        self.deps = map(lambda i: AtomId(i), json['deps'])
        self.package_deps = map(lambda i: AtomId(i), json['package-deps'])
        self.files = [File(f) for f in json['files']]
        self.tags = json['tags']

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


CATEGORIES = [
    'excluded',
    'experimental',
    'internal',
    'partner',
    'public',
]


def index_for_category(category):
    if not category in CATEGORIES:
        raise Exception('Unknown SDK category "%s"' % category)
    return CATEGORIES.index(category)


def detect_category_violations(category, atoms):
    '''Detects mismatches in publication categories.'''
    has_violations = False
    category_index = index_for_category(category)
    for atom in atoms:
        if index_for_category(atom.category) < category_index:
            has_violations = True
            print('%s has publication level %s, incompatible with %s' % (
                    atom, atom.category, category))
    return has_violations
