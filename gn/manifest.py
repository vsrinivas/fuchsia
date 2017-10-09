#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import namedtuple
import os


manifest_entry = namedtuple('manifest_entry', [
    'group',
    'target',
    'source',
    'manifest',
])

def format_manifest_file(manifest):
    return ''.join((('' if entry.group is None else '{' + entry.group + '}') +
                    entry.target + '=' + entry.source + '\n')
                   for entry in manifest)


def read_manifest_file(file, manifest_cwd, result_cwd):
    for line in file:
        # Remove the trailing newline.
        assert line.endswith('\n'), "Unterminated manifest line: %r" % line
        line = line[:-1]

        # Grok {group}... syntax.
        group = None
        if line.startswith('{'):
            end = line.find('}')
            assert end > 0, "Unterminated { in manifest line: %r" % line
            group = line[1:end]
            line = line[end + 1:]

        # Grok target=source syntax.
        [target_file, build_file] = line.split('=', 1)

        # Expand the path based on the cwd presumed in the manifest.
        build_file = os.path.normpath(os.path.join(manifest_cwd, build_file))

        # Make it relative to the cwd we want to work from.
        build_file = os.path.relpath(build_file, result_cwd)

        if 'prebuilt' not in build_file:
            yield manifest_entry(group, target_file, build_file, file.name)


def partition_manifest(manifest, select, selected_group, unselected_group):
    selected = []
    unselected = []
    for entry in manifest:
        if select(entry.group):
            selected.append(entry._replace(group=selected_group))
        else:
            unselected.append(entry._replace(group=unselected_group))
    return selected, unselected


def ingest_manifest_file(filename, in_cwd, groups, out_cwd, output_group):
    groups_seen = set()
    def select(group):
        groups_seen.add(group)
        if isinstance(groups, bool):
            return groups
        return group in groups
    with open(filename, 'r') as file:
        selected, unselected = partition_manifest(
            read_manifest_file(file, in_cwd, out_cwd),
            select, output_group, None)
        return selected, unselected, groups_seen


def test_main(filename, manifest_cwd, result_cwd, groups):
    if groups == 'all':
        groups = True
    else:
        groups = set(group if group else None for group in groups.split(','))
    selected, unselected, groups_seen = ingest_manifest_file(
        filename, manifest_cwd, groups, result_cwd, None)
    for manifest, title in [(selected, 'Selected:'),
                            (unselected, 'Unselected:')]:
        manifest.sort(key=lambda entry: entry.target)
        print title
        print format_manifest_file(manifest)
    print 'Groups seen: %r' % groups_seen
    if not isinstance(groups, bool):
        print 'Unused groups: %r' % (groups - groups_seen)


# For manual testing.
if __name__ == "__main__":
    import sys
    test_main(*sys.argv[1:])
