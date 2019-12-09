#!/usr/bin/env python2.7
#
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Make a mapping of dependencies for fidl files.

The map contains one line per mapping, each of which is from a fidl file location
to a sequence of GN build targets. If the mapping is public (ie directly from the
source or through a public_deps dependency) then it is prepended with "pub ".

The sequence of targets has length greater than 1 if the file can be referenced
from this module through a sequence of dependencies.

An example entry is:

pub //garnet/public/lib/ui/views/fidl/views.fidl: //garnet/public/lib/media/fidl:services //garnet/public/lib/ui/base_view/cpp:cpp
"""

import ast
import optparse
import os
import sys
import zipfile


# for some reason, gn seems to tack on an extra element in the directory
def fix_label(label):
    split_tag = label.split(':')
    split_label = split_tag[0].split('/')
    split_label.pop()
    split_tag[0] = '/'.join(split_label)
    return ':'.join(split_tag)


def rel_label(label, relpath):
    split_label = label.split(':')[0].split('/')
    for el in relpath.split('/'):
        if el == '..':
            split_label.pop()
        else:
            split_label.append(el)
    return '/'.join(split_label)


def handle_map_deps(o, depsmap, covered, target, is_public):
    #o.write('#pass-through from %s (public %s)\n' % (depsmap, is_public))
    for dep in file(depsmap):
        parts = dep.split()
        if parts[0] != 'pub':
            continue
        fileref = parts[1].rstrip(':')
        if fileref in covered:
            continue
        pubstr = 'pub ' if is_public else ''
        o.write(
            '%s%s: %s %s\n' % (pubstr, fileref, target, ' '.join(parts[2:])))


def make_map(output, sources, map_deps, map_public_deps, target):
    target = fix_label(target)
    o = file(output, 'w')
    covered = set()
    for i in sources:
        covered.add(i)
        new_path = rel_label(target, i)
        o.write('pub %s: %s\n' % (new_path, target))
    for m in map_deps:
        handle_map_deps(o, m, covered, target, False)
    for m in map_public_deps:
        handle_map_deps(o, m, covered, target, True)


def main():
    parser = optparse.OptionParser()

    parser.add_option('--sources', help='List of source files.')
    parser.add_option('--target', help='Name of current target.')
    parser.add_option(
        '--map-deps', help='List of map files of deps to aggregate.')
    parser.add_option(
        '--map-public-deps',
        help='List of map files of public deps to aggregate.')
    parser.add_option('--output', help='Path to output archive.')

    options, _ = parser.parse_args()

    sources = []
    if (options.sources):
        sources = ast.literal_eval(options.sources)
    map_deps = []
    if options.map_deps:
        map_deps = ast.literal_eval(options.map_deps)
    map_public_deps = []
    if options.map_public_deps:
        map_public_deps = ast.literal_eval(options.map_public_deps)
    output = options.output
    target = options.target

    make_map(output, sources, map_deps, map_public_deps, target)


if __name__ == '__main__':
    sys.exit(main())
