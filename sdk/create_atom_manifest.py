#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

from sdk_common import Atom, AtomId, detect_category_violations, detect_collisions, gather_dependencies


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
    parser.add_argument('--package-deps',
                        help='List of manifest paths for runtime dependencies',
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
    parser.add_argument('--tags-file',
                        help='A file containing tags',
                        required=False)
    parser.add_argument('--gn-label',
                        help='GN label of the atom',
                        required=True)
    parser.add_argument('--category',
                        help='Publication level',
                        required=True)
    args = parser.parse_args()

    if args.name:
        name = args.name
    else:
        with open(args.name_file, 'r') as name_file:
            name = name_file.read()

    # Gather the definitions of other atoms this atom depends on.
    (deps, atoms) = gather_dependencies(args.deps)
    (_, package_atoms) = gather_dependencies(args.package_deps)
    all_atoms = atoms
    all_atoms.update(package_atoms)

    # Build the list of files making up this atom.
    files = []
    has_packaged_files = False
    base = os.path.realpath(args.base)
    mappings = args.files
    if args.file_manifest:
        with open(args.file_manifest, 'r') as manifest_file:
            additional_mappings = [l.strip() for l in manifest_file.readlines()]
            mappings.extend(additional_mappings)
    for mapping in mappings:
        mode, pair = mapping.split(':', 1)
        is_packaged = (mode == 'packaged')
        destination, source = pair.split('=', 1)
        real_source = os.path.realpath(source)
        if not os.path.exists(real_source):
            raise Exception('Missing source file: %s' % real_source)
        if destination:
            if destination.find('..') != -1:
                raise Exception('Destination for %s cannot contain "..": %s.' %
                                (source, destination))
        else:
            if not real_source.startswith(base):
                raise Exception('Destination for %s must be given as it is not'
                                ' under source directory %s' % (source, base))
            destination = os.path.relpath(real_source, base)
        if os.path.isabs(destination):
            raise Exception('Destination cannot be absolute: %s' % destination)
        files.append({
            'source': real_source,
            'destination': destination,
            'packaged': is_packaged
        })
        has_packaged_files = has_packaged_files or is_packaged

    id = {
        'domain': args.domain,
        'name': name,
    }

    all_package_deps = set()
    if has_packaged_files:
        all_package_deps.add(AtomId(id))
    for atom in all_atoms:
        all_package_deps.update(atom.package_deps)

    tags = dict(map(lambda t: t.split(':', 1), args.tags))
    if args.tags_file:
        with open(args.tags_file, 'r') as tags_file:
            data = json.load(tags_file)
            assert isinstance(data, dict)
            tags.update(data)
    tags['domain'] = args.domain

    all_atoms.update([Atom({
        'id': id,
        'gn-label': args.gn_label,
        'category': args.category,
        'tags': tags,
        'deps': map(lambda i: i.json, sorted(list(deps))),
        'package-deps': map(lambda i: i.json, sorted(list(all_package_deps))),
        'files': files,
    })])
    if detect_collisions(all_atoms):
        print('Name collisions detected!')
        return 1
    if detect_category_violations(args.category, all_atoms):
        print('Publication level violations detected!')
        return 1

    manifest = {
        'ids': [id],
        'atoms': map(lambda a: a.json, sorted(list(all_atoms))),
    }

    with open(os.path.abspath(args.out), 'w') as out:
        json.dump(manifest, out, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
