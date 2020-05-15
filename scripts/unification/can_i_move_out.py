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


class BuildGraph:

    def __init__(self, deps):
        self._deps = deps
        self._compute_refs()

    def _compute_refs(self):
        self._nodes = set(self._deps.keys())
        self._nodes.update(set(dep for v in self._deps.values() for dep in v))
        refs = {}

        for node in self._nodes:
            if node not in refs:
                refs[node] = []
            if node not in self._deps:
                self._deps[node] = []

        for label, label_deps in self._deps.items():
            if label not in refs:
                refs[label] = []
            for dep in label_deps:
                if dep not in refs:
                    refs[dep] = []
                refs[dep].append(label)

        self._refs = refs

    def collapse_nodes(self, transform):
        '''
        Rename each node in the graph by passing it through the transform function,
        and merge nodes with the same name. The new node will have the union of
        edges on the old nodes. Nodes for which the transform function returns
        None will be deleted.
        '''
        new_graph = collections.defaultdict(set)
        for src, dsts in self._deps.items():
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
        self._deps = dict(new_graph)
        self._compute_refs()

    def references(self, label):
        return self._refs.get(label)

    def dependencies(self, label):
        return self._deps.get(label)


def load_build_graph(gn_binary, zircon_dir, build_dir):
    root_label = '//:default'
    output = subprocess.check_output(
        [
            gn_binary, '--root=' + zircon_dir, 'desc', build_dir, root_label,
            'deps', '--tree'
        ],
        encoding='utf8')

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

    return BuildGraph(deps)


def lib_label(type, name):
    return '//system/{}/{}'.format(type, name)


def format_label(label):
    if label.startswith('//system/'):
        label = label[len('//system/'):]
    return label


def find_libraries(zircon_dir, type):
    base = os.path.join(zircon_dir, 'system', type)

    def has_zn_build_file(dir):
        build_path = os.path.join(base, dir, 'BUILD.gn')
        if not os.path.isfile(build_path):
            return False
        with open(build_path, 'r') as build_file:
            content = build_file.read()
            return (
                '//build/unification/zx_library.gni' not in content and
                '//build/unification/fidl_alias.gni' not in content and
                'Fuchsia GN build' not in content)

    for _, dirs, _ in os.walk(base):
        return [lib_label(type, dir) for dir in dirs if has_zn_build_file(dir)]


def unblocked_by(graph, label):
    deps = graph.dependencies(label)
    unblocked = []
    if deps is None:
        return []
    for dep in deps:
        if graph.references(dep) == [label]:
            unblocked.append(dep)
    return unblocked


def main():
    parser = argparse.ArgumentParser(
        'Determines whether libraries can be '
        'moved out of the ZN build')
    parser.add_argument(
        '--build-dir',
        help='Path to the ZN build dir',
        default=os.path.join(FUCHSIA_ROOT, 'out', 'default.zircon'))
    parser.add_argument(
        '--plain',
        help='Only print library names, no dependency information',
        action='store_true')
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

    graph = load_build_graph(gn_binary, zircon_dir, build_dir)

    def strip_toolchain_and_label(label):
        dirname, _, _ = label.partition(':')
        for type in ['fidl', 'banjo', 'ulib', 'dev/lib']:
            if dirname == '//system/' + type:
                return
        return dirname

    graph.collapse_nodes(strip_toolchain_and_label)

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

        refs = graph.references(lib_label(type, name))

        if refs is None:
            print('Could not find "%s", please check spelling!' % args.name)
            return 1
        elif len(refs):
            print('Nope, there are still references in the ZN build:')
            shown = set()
            batch = set(refs)
            while batch:
                for ref in sorted(batch):
                    print('  ' + format_label(ref))
                shown.update(batch)
                new_batch = set()
                for ref in batch:
                    for new_ref in graph.references(ref):
                        if new_ref not in shown:
                            new_batch.add(new_ref)
                batch = new_batch
                if batch:
                    print('---')
            return 2
        else:
            print('Yes you can!')

        return 0

    # Case 2: no library name given.
    libs = find_libraries(zircon_dir, type)
    movable = set()
    for lib in libs:
        refs = graph.references(lib)
        if not refs:
            movable.add(lib)
    if movable:
        if not args.plain:
            print('These libraries are free to go:')
        for label in sorted(movable):
            print('  ' + format_label(label))
            if args.plain:
                continue
            unblocked = unblocked_by(graph, label)
            if unblocked:
                print(
                    '    would unblock:',
                    ', '.join(format_label(u) for u in unblocked))
            if not graph.dependencies(label):
                print('    no dependencies')

    else:
        print('No library may be moved')

    return 0


if __name__ == '__main__':
    sys.exit(main())
