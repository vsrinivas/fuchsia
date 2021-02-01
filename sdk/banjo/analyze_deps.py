#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Produces a representation of the dependencies between Banjo libraries.'''

import argparse
import copy
import json
import re
import sys

_LIBRARY_LABEL = r'^//sdk/banjo/([^:]+):\1(\.\.\.)?$'

_DUMMY_LIBRARIES = set(
    [
        'ddk.driver',
        'ddk.physiter',
        'zircon.hw.pci',
        'zircon.hw.usb',
        'zircon.syscalls.pci',
    ])


def _extract_dependencies(label, base_depth, deps, result):
    index = 0
    current_label = None
    dep_labels = []
    while index < len(deps):
        depth = deps[index].index('//')
        if depth <= base_depth:
            break
        elif depth == base_depth + 2:
            current_label = re.sub(r'\.\.\.', '', deps[index]).strip()
            dep_labels.append(current_label)
            index += 1
        else:
            index += _extract_dependencies(
                current_label, base_depth + 2, deps[index:], result)
    result[label] = dep_labels
    return index


def extract_dependencies(deps):
    result = {}
    _extract_dependencies('main', -2, deps, result)
    return result


def filter_banjo_libraries(deps):
    # This filters dep lists to retain only Banjo libraries.
    normalize_deps = lambda l: list(
        filter(lambda i: i, map(lambda c: get_library_label(c), l)))

    # Retain only Banjo libraries at the top level and normalize their dep lists.
    result = dict(
        map(
            lambda r: (r[0], normalize_deps(r[1])),
            filter(
                lambda t: t[0],
                map(lambda p: (get_library_label(p[0]), p[1]), deps.items()))))

    # Add all standalone libraries.
    for lib in normalize_deps(deps['main']):
        if lib not in result:
            result[lib] = []

    return result


def get_library_label(label):
    match = re.match(_LIBRARY_LABEL, label)
    return match.group(1) if match else None


def remove_library(name, deps):
    if name in deps:
        del deps[name]
    for v in deps.values():
        if name in v:
            v.remove(name)


def add_back_edges(deps):
    result = copy.deepcopy(deps)
    for lib, lib_deps in deps.items():
        for d in lib_deps:
            result[d].append(lib)
    return result


def find_connected_components(deps):

    def find_component(library, component):
        connections = deps.pop(library)
        component.add(library)
        for c in connections:
            if c not in deps:
                continue
            find_component(c, component)
        return component

    result = []
    while deps:
        result.append(find_component(list(deps.keys())[0], set()))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--gn-deps',
        help='Path to the JSON-formatted files with dependency info',
        required=True)
    args = parser.parse_args()

    with open(args.gn_deps, 'r') as deps_file:
        deps = json.load(deps_file)

    all_deps = extract_dependencies(deps['//sdk/banjo:banjo']['deps'])
    banjo_deps = filter_banjo_libraries(all_deps)
    remove_library('zx', banjo_deps)
    remove_library('fuchsia.hardware.composite', banjo_deps)
    banjo_graph = add_back_edges(banjo_deps)

    components = find_connected_components(banjo_graph)

    blocked = filter(lambda g: g & _DUMMY_LIBRARIES, components)
    if blocked:
        print()
        print('Blocked by dummy libraries')
        all_blocked = set()
        for group in blocked:
            all_blocked |= group
        for library in sorted(all_blocked - _DUMMY_LIBRARIES):
            print(' - ' + library)

    standalones = filter(
        lambda s: len(s) == 1 and not s & _DUMMY_LIBRARIES, components)
    if standalones:
        print()
        print('Standalone:')
        for singleton in standalones:
            print(' - ' + next(iter(singleton)))

    groups = map(
        lambda t: t[1],
        sorted(
            map(
                lambda s: (len(s), s),
                filter(
                    lambda g: len(g) > 1 and not g & _DUMMY_LIBRARIES,
                    components))))
    if groups:
        print()
        print('Groups:')
        for index, group in enumerate(groups):
            print('[' + str(index) + ']')
            for library in sorted(group):
                print(' - ' + library)

    return 0


if __name__ == '__main__':
    sys.exit(main())
