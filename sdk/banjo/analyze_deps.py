#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Produces a representation of the dependencies between Banjo libraries.'''

import argparse
import json
import re
import sys

_LIBRARY_LABEL = r'^//sdk/banjo/([^:]+):\1(\.\.\.)?$'


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


def filter_banjo_libaries(deps):
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
    banjo_deps = filter_banjo_libaries(all_deps)

    print(
        json.dumps(
            banjo_deps, indent=2, sort_keys=True, separators=(',', ': ')))

    return 0


if __name__ == '__main__':
    sys.exit(main())
