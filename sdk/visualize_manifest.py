#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def sanitize_name(name):
    '''Makes a given string usable in a label.'''
    return name.replace('-', '_')


def get_atom_id(id):
    '''Returns a string representing a sanitized version of the given id.'''
    return '%s__%s' % (sanitize_name(id['domain']), sanitize_name(id['name']))


def main():
    parser = argparse.ArgumentParser(
            description=('Generates a DOT file representing the contents of an '
                         'SDK manifest'))
    parser.add_argument('--manifest',
                        help='Path to the SDK manifest',
                        required=True)
    parser.add_argument('--output',
                        help=('Path to the DOT file to produce, '
                               'defaults to <manifest_name>.dot'))
    args = parser.parse_args()

    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)

    all_atoms = manifest['atoms']
    domains = set([a['id']['domain'] for a in all_atoms])

    if args.output is not None:
        output = args.output
    else:
        output = '%s.dot' % os.path.basename(args.manifest).split('.', 1)[0]

    with open(output, 'w') as out:
        out.write('digraph fuchsia {\n')
        for index, domain in enumerate(domains):
            out.write('subgraph cluster_%s {\n' % index)
            out.write('label="%s";\n' % domain)
            atoms = [a for a in all_atoms if a['id']['domain'] == domain]
            for atom in atoms:
                out.write('%s [label="%s"];\n' % (get_atom_id(atom['id']),
                                                  atom['id']['name']))
            out.write('}\n')
        for atom in all_atoms:
            if not atom['deps']:
                continue
            id = get_atom_id(atom['id'])
            dep_ids = [get_atom_id(d) for d in atom['deps']]
            out.write('%s -> { %s }\n' % (id, ' '.join(dep_ids)));
        out.write('}\n')


if __name__ == '__main__':
    sys.exit(main())
