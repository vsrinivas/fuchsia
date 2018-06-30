#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import namedtuple
import argparse
import os
import errno
import fnmatch
import shlex
import shutil
import sys


manifest_entry = namedtuple('manifest_entry', [
    'group',
    'target',
    'source',
    'manifest',
])


def format_manifest_entry(entry):
    return (('' if entry.group is None else '{' + entry.group + '}') +
            entry.target + '=' + entry.source)


def format_manifest_file(manifest):
    return ''.join(format_manifest_entry(entry) + '\n' for entry in manifest)


def read_manifest_lines(lines, title, manifest_cwd, result_cwd):
    for line in lines:
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

        if manifest_cwd != result_cwd:
            # Expand the path based on the cwd presumed in the manifest.
            build_file = os.path.normpath(os.path.join(manifest_cwd,
                                                       build_file))
            # Make it relative to the cwd we want to work from.
            build_file = os.path.relpath(build_file, result_cwd)

        # TODO(mcgrathr): Dismal kludge to avoid pulling in asan runtime
        # libraries from the zircon ulib bootfs.manifest because their source
        # file names don't match the ones in the toolchain manifest.
        if 'prebuilt/downloads/clang/lib/' in build_file:
            continue

        yield manifest_entry(group, target_file, build_file, title)


def partition_manifest(manifest, select, selected_group, unselected_group):
    selected = []
    unselected = []
    for entry in manifest:
        if select(entry.group):
            selected.append(entry._replace(group=selected_group))
        else:
            unselected.append(entry._replace(group=unselected_group))
    return selected, unselected


def ingest_manifest_lines(lines, title, in_cwd, groups, out_cwd, output_group):
    groups_seen = set()
    def select(group):
        groups_seen.add(group)
        if isinstance(groups, bool):
            return groups
        return group in groups
    selected, unselected = partition_manifest(
        read_manifest_lines(lines, title, in_cwd, out_cwd),
        select, output_group, None)
    return selected, unselected, groups_seen


def apply_source_rewrites(rewrites, entry):
    for rewrite in rewrites:
        if entry.source == rewrite.source:
            return entry._replace(source=rewrite.target)
    return entry


def apply_rewrites(rewrites, entry):
    for pattern, line in rewrites:
        if fnmatch.fnmatchcase(entry.target, pattern):
            [new_entry] = read_manifest_lines(
                [line.format(**entry._asdict()) + '\n'], entry.manifest,
                os.path.dirname(entry.manifest),
                os.path.dirname(entry.manifest))
            entry = new_entry._replace(group=entry.group)
    return entry


def contents_entry(entry):
    with open(entry.source) as file:
        [line] = file.read().splitlines()
        return entry._replace(source=line)


class input_action_base(argparse.Action):
    def __init__(self, *args, **kwargs):
        super(input_action_base, self).__init__(*args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        outputs = getattr(namespace, 'output', None)

        all_selected = getattr(namespace, 'selected', None)
        if all_selected is None:
            all_selected = []
            setattr(namespace, 'selected', all_selected)
        all_unselected = getattr(namespace, 'unselected', None)
        if all_unselected is None:
            all_unselected = []
            setattr(namespace, 'unselected', all_unselected)

        if namespace.groups is None or outputs is None:
            groups = False
        elif namespace.groups == 'all':
            groups = True
        else:
            groups = set(group if group else None
                         for group in namespace.groups.split(','))

        cwd = getattr(namespace, 'cwd', '')

        if outputs is not None:
            output_group = len(outputs) - 1
        else:
            output_group = None

        selected, unselected, groups_seen = self.get_manifest_lines(
            namespace, values, cwd, groups, namespace.output_cwd, output_group)

        include = getattr(namespace, 'include', [])
        exclude = getattr(namespace, 'exclude', [])
        if include or exclude:
            def included(entry):
                return (entry.target not in exclude and
                        (not include or entry.target in include))
            unselected += filter(lambda entry: not included(entry), selected)
            selected = filter(included, selected)

        if getattr(namespace, 'contents', False):
            selected = map(contents_entry, selected);
            unselected = map(contents_entry, unselected);

        rewrites = [entry.split('=', 1)
                     for entry in getattr(namespace, 'rewrite', [])]
        selected = [apply_rewrites(rewrites, entry) for entry in selected]
        unselected = [apply_rewrites(rewrites, entry) for entry in unselected]

        if not isinstance(groups, bool):
            unused_groups = groups - groups_seen - set([None])
            if unused_groups:
                raise Exception(
                    '%s not found in %r; try one of: %s' %
                    (', '.join(map(repr, unused_groups)), values,
                     ', '.join(map(repr, groups_seen - groups))))

        all_selected += selected
        all_unselected += unselected


class input_manifest_action(input_action_base):
    def __init__(self, *args, **kwargs):
        super(input_manifest_action, self).__init__(*args, **kwargs)

    def get_manifest_lines(self, namespace, filename, *args):
        all_inputs = getattr(namespace, 'manifest', None)
        if all_inputs is None:
            all_inputs = []
            setattr(namespace, 'manifest', all_inputs)
        all_inputs.append(filename)
        with open(filename, 'r') as file:
            return ingest_manifest_lines(file, file.name, *args)


class input_entry_action(input_action_base):
    def __init__(self, *args, **kwargs):
        super(input_entry_action, self).__init__(*args, **kwargs)

    def get_manifest_lines(self, namespace, entry, *args):
        return ingest_manifest_lines(
            [entry + '\n'], namespace.entry_manifest, *args)


def common_parse_args(parser):
    parser.add_argument('--output', action='append', required=True,
                        metavar='FILE',
                        help='Output file')
    parser.add_argument('--output-cwd', default='',
                        metavar='DIRECTORY',
                        help='Emit source paths relative to DIRECTORY')
    parser.add_argument('--absolute', action='store_true', default=False,
                        help='Output source file names as absolute paths')
    parser.add_argument('--cwd', default='',
                        metavar='DIRECTORY',
                        help='Input entries are relative to this directory')
    parser.add_argument('--groups', default='all',
                        metavar='GROUP_LIST',
                        help='"all" or comma-separated groups to include')
    parser.add_argument('--manifest', action=input_manifest_action,
                        metavar='FILE', default=[],
                        help='Input manifest file (must exist)')
    parser.add_argument('--entry', action=input_entry_action,
                        metavar='PATH=FILE',
                        help='Add a single entry as if from an input manifest')
    parser.add_argument('--entry-manifest', default='<command-line --entry>',
                        metavar='TITLE',
                        help=('Title in lieu of manifest file name for' +
                              ' subsequent --entry arguments'))
    parser.add_argument('--include', action='append', default=[],
                        metavar='TARGET',
                        help='Include only input entries matching TARGET'),
    parser.add_argument('--reset-include',
                        action='store_const', const=[], dest='include',
                        help='Reset previous --include')
    parser.add_argument('--exclude', action='append', default=[],
                        metavar='TARGET',
                        help='Ignore input entries matching TARGET'),
    parser.add_argument('--reset-exclude',
                        action='store_const', const=[], dest='exclude',
                        help='Reset previous --exclude')
    # Replace each `@rspfile` with the arguments from the file, and iterate.
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i][0] == '@':
            with open(args[i][1:]) as rsp_file:
                args[i:i + 1] = shlex.split(rsp_file)
        else:
            i += 1
    return parser.parse_args(args)


def parse_args():
    parser = argparse.ArgumentParser(description='Read manifest files.')
    parser.add_argument('--copytree', action='store_true', default=False,
                        help='Output directory tree of copies, not manifest')
    parser.add_argument('--sources', action='store_true', default=False,
                        help='Write source file per line, not manifest entry')
    parser.add_argument('--contents',
                        action='store_true', default=False,
                        help='Replace each source file name with its contents')
    parser.add_argument('--no-contents',
                        action='store_false', dest='contents',
                        help='Reset previous --contents')
    parser.add_argument(
        '--rewrite', action='append', default=[],
        metavar='PATTERN=ENTRY',
        help='Replace entries whose target matches PATTERN with ENTRY,'
        ' which can use {source} and {target} substitutions'),
    parser.add_argument('--reset-rewrite', dest='rewrite',
                        action='store_const', const=[],
                        help='Reset previous --rewrite')
    parser.add_argument('--unique',
                        action='store_true', default=False,
                        help='Elide duplicates even with different sources')
    parser.add_argument('--stamp',
                        metavar='FILE',
                        help='Touch FILE at the end.')
    args = common_parse_args(parser)
    if args.copytree:
        if args.contents:
            parser.error('--copytree is incompatible with --contents')
        args.unique = True
        args.sources = True
    return args


def main():
    args = parse_args()
    output_sets = [(dict() if args.unique else set()) for file in args.output]
    for entry in getattr(args, 'selected', []):
        assert entry.group is not None, entry
        if args.absolute:
            line = os.path.abspath(entry.source)
        else:
            line = entry.source
        if not args.sources:
            line = entry.target + '=' + line
        if args.unique:
            output_sets[entry.group][entry.target] = line
        else:
            output_sets[entry.group].add(line)
    for output_filename, output_set in zip(args.output, output_sets):
        if args.copytree:
            for target, source in output_set.iteritems():
                try:
                    os.makedirs(os.path.join(output_filename,
                                             os.path.dirname(target)))
                except OSError as exc:
                    if exc.errno != errno.EEXIST:
                        raise exc
                shutil.copyfile(source, os.path.join(output_filename, target))
        else:
            with open(output_filename, 'w') as file:
                file.write(''.join(sorted(
                    line + '\n' for line in
                    (output_set.itervalues() if args.unique else output_set))))
    if args.stamp:
        with open(args.stamp, 'w') as file:
            os.utime(file.name, None)


if __name__ == "__main__":
    main()
