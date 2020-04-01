#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    SCRIPT_DIR))                 # unification


class Finder(object):

    def __init__(self, gn_binary, zircon_dir, build_dir):
        self._zircon_dir = zircon_dir
        self._command = [
            gn_binary, '--root=' + zircon_dir, 'desc', build_dir, '//:default',
            'deps', '--tree'
        ]
        self.load_build_graph()

    def load_build_graph(self):
        root_label = '//:default'
        output = subprocess.check_output(self._command, encoding='utf8')

        # GN outputs the graph in an indented tree format:
        # //foo
        #   //bar
        #     //baz
        # //bar...
        # The ... means this label has already been visited

        deps = collections.defaultdict(list)
        label_stack = [root_label]
        for line in output.splitlines():
            label = line.lstrip()
            leading_spaces = len(line) - len(label)
            assert leading_spaces >= 0
            assert leading_spaces % 2 == 0
            if label.endswith('...'):
                label = label[:-3]
            label_depth = leading_spaces // 2
            del label_stack[label_depth + 1:]
            label_stack.append(label)
            deps[label_stack[label_depth]].append(label)

        def strip_toolchain(label):
            if label.endswith(')'):
                label = label[:label.rindex('(')]
            return label

        self._deps = collapse_nodes(deps, strip_toolchain)

        refs = collections.defaultdict(list)
        for label, label_deps in self._deps.items():
            for dep in label_deps:
                refs[dep].append(label)
        self._refs = dict(refs)

    def find_references(self, type, name):
        category_label = '//system/' + type
        base_label = '//system/' + type + '/' + name

        all_refs = set()
        for label, refs in self._refs.items():
            if label.startswith(base_label + ':'):
                all_refs.update(set(refs))

        same_type_references = set()
        other_type_references = set()
        for line in all_refs:
            line = line.strip()
            if line.startswith(base_label + ':'):
                continue
            # Remove target name and toolchain.
            line = line[0:line.find(':')]
            if line == category_label:
                continue
            same_type = line.startswith(category_label)
            # Insert 'zircon' directory at the start.
            line = '//zircon' + line[1:]
            refs = same_type_references if same_type else other_type_references
            refs.add(line)

        return same_type_references, other_type_references

    def find_libraries(self, type):
        base = os.path.join(self._zircon_dir, 'system', type)

        def has_zn_build_file(dir):
            build_path = os.path.join(base, dir, 'BUILD.gn')
            if not os.path.isfile(build_path):
                return False
            with open(build_path, 'r') as build_file:
                content = build_file.read()
                return (
                    '//build/unification/zx_library.gni' not in content and
                    '//build/unification/fidl_alias.gni' not in content)

        for _, dirs, _ in os.walk(base):
            return list(filter(has_zn_build_file, dirs))


def collapse_nodes(graph, transform):
    '''
    Rename each node in the graph by passing it through the transform function,
    and merge nodes with the same name. The new node will have the union of
    edges on the old nodes. Returns a new graph.
    '''
    new_graph = collections.defaultdict(set)
    for src, dsts in graph.items():
        xsrc = transform(src)
        if xsrc is None:
            continue
        src_set = new_graph[xsrc]
        for dst in dsts:
            xdst = transform(dst)
            if xdst is None:
                continue
            if xsrc == xdst:
                continue
            src_set.add(xdst)
    return dict(new_graph)


def main():
    parser = argparse.ArgumentParser(
        'Determines whether libraries can be '
        'moved out of the ZN build')
    parser.add_argument(
        '--build-dir',
        help='Path to the ZN build dir',
        default=os.path.join(FUCHSIA_ROOT, 'out', 'default.zircon'))
    type = parser.add_mutually_exclusive_group(required=True)
    type.add_argument(
        '--banjo', help='Inspect Banjo libraries', action='store_true')
    type.add_argument(
        '--fidl', help='Inspect FIDL libraries', action='store_true')
    type.add_argument(
        '--ulib', help='Inspect C/C++ libraries', action='store_true')
    type.add_argument(
        '--devlib',
        help='Inspect C/C++ libraries under dev',
        action='store_true')
    parser.add_argument(
        'name',
        help='Name of the library to inspect; if empty, scan '
        'all libraries of the given type',
        nargs='?')
    args = parser.parse_args()

    source_dir = FUCHSIA_ROOT
    zircon_dir = os.path.join(source_dir, 'zircon')
    build_dir = os.path.abspath(args.build_dir)

    if sys.platform.startswith('linux'):
        platform = 'linux-x64'
    elif sys.platform.startswith('darwin'):
        platform = 'mac-x64'
    else:
        print('Unsupported platform: %s' % sys.platform)
        return 1
    gn_binary = os.path.join(
        source_dir, 'prebuilt', 'third_party', 'gn', platform, 'gn')

    finder = Finder(gn_binary, zircon_dir, build_dir)

    if args.fidl:
        type = 'fidl'
    elif args.banjo:
        type = 'banjo'
    elif args.ulib:
        type = 'ulib'
    elif args.devlib:
        type = 'dev/lib'

    # Case 1: a library name is given.
    if args.name:
        name = args.name
        if args.fidl:
            # FIDL library names use the dot separator, but folders use an
            # hyphen: be nice to users and support both forms.
            name = name.replace('.', '-')

        same_type_refs, other_type_refs = finder.find_references(type, name)

        if same_type_refs is None:
            print('Could not find "%s", please check spelling!' % args.name)
            return 1
        elif same_type_refs or other_type_refs:
            print('Nope, there are still references in the ZN build:')
            for ref in sorted(same_type_refs | other_type_refs):
                print('  ' + ref)
            return 2
        else:
            print('Yes you can!')

        return 0

    # Case 2: no library name given.
    names = finder.find_libraries(type)
    candidates = {}
    for name in names:
        same_type_refs, other_type_refs = finder.find_references(type, name)
        if other_type_refs:
            continue
        candidates[name] = same_type_refs
    movable = {n for n, r in candidates.items() if not r}
    type_blocked = {
        n for n, r in candidates.items() if r and set(r).issubset(movable)
    }
    if type_blocked:
        print(
            'These libraries are only blocked by libraries waiting to be moved:'
        )
        for name in sorted(type_blocked):
            print('  ' + name + ' (' + ','.join(candidates[name]) + ')')
    if movable:
        print('These libraries are free to go:')
        for name in sorted(movable):
            print('  ' + name)
    else:
        print('No library may be moved')

    return 0


if __name__ == '__main__':
    sys.exit(main())
