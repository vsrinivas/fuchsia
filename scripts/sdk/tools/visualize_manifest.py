#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys
import urlparse


def sanitize_name(name):
    '''Makes a given string usable in a label.'''
    return name.replace('-', '_').replace('.', '_')


def get_atom_domain(id):
    return urlparse.urlparse(id).netloc


def get_atom_name(id):
    return urlparse.urlparse(id).path[1:]


def get_atom_id(id):
    return sanitize_name(get_atom_name(id))


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
    domains = set([get_atom_domain(a['id']) for a in all_atoms])

    if args.output is not None:
        output = args.output
    else:
        output = '%s.dot' % os.path.basename(args.manifest).split('.', 1)[0]

    with open(output, 'w') as out:
        out.write('digraph fuchsia {\n')
        for index, domain in enumerate(domains):
            out.write('subgraph cluster_%s {\n' % index)
            out.write('label="%s";\n' % domain)
            atoms = [a for a in all_atoms if get_atom_domain(a['id']) == domain]
            for atom in atoms:
                out.write('%s [label="%s"];\n' % (get_atom_id(atom['id']),
                                                  get_atom_name(atom['id'])))
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
