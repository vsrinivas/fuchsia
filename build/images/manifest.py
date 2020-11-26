#!/usr/bin/env python3.8
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

# TECHNICAL NOTE:
#
# This Python file serves both as a standalone script, and a utility module, to
# read and write ZBI manifests, which are simple text files where each line
# should look like:
#
#   <target-path>=<source-path>
#
# Where <target-path> is an destination/installation path, e.g. within a Fuchsia
# package, and <source-path> is the location of the corresponding source content.
#
# Each manifest entry will be identified by a |manifest_entry| instance (see below),
# which records the entry's target and source paths, the input manifest it appears
# in, and an optional output 'group' index (more on this later).
#
# The core feature implemented here is to parse command-line arguments to build
# two manifest_entry lists:
#
#  - A list of 'selected' entries that will be output to one of the output
#    manifest specified on the command-line through '--output=FILE'. Each such entry
#    has a 'group' field which indicates which output file it must be written to.
#
#  - A list of 'unselected' entries, either because they appeared before the
#    first '--output=FILE' argument, or because they were excluded from selection
#    through the use of '--exclude' and '--include' options. Each such entry
#    as a 'group' value of None.
#
# Input manifest entries can be fed to the command line in two ways:
#
#  * Using --manifest=FILE to read an existing ZBI manifest file.
#
#  * Using --entry=TARGET=SOURCE directly, where TARGET and SOURCE are target and
#    source paths for the entry. This requires at least one previous
#    --entry-manifest=PATH argument, which will be used to populate the entry's
#    |manifest| field.
#
# Note that each source path will be relative to the latest --cwd=DIR argument
# that appeared on the command line, with a default value of '.'.
#
# An output manifest can be specified with --output=FILE. This argument can be used
# several times to generate several output manifests.
#
# Any input entry that appears before the first --output argument goes to the
# unselected list.
#
# Input entries that appear after an --output argument are assigned to be written
# to the correspondind output manifest file, by default, through --exclude and
# --include options described below can result in such an entry to be unselected
# as well.
#
# Many command line options control state during parsing that will either filter
# or transform the input entries before they are recorded into one of the two
# output lists. See their description in common_parse_args() and parse_args()
# below.
#
# Note that some scripts use this Python module to first build a first version
# of the 'unselected' and 'selected' lists, and will later modify these lists
# before actually writing to the output manifest(s). For example to move
# unselected shared library dependencies to the selected list if some selected
# binaries depend on them.
#

# Describes a manifest entry:
#   group: Either None, if the entry doesn't need to be written to any output
#      manifest, or an integer index into the list of --output=FILE arguments
#      otherwise.
#   target: Target installation path.
#   source: Source path for the entry.
#   manifest: Path to input manifest this entry belongs to.
manifest_entry = namedtuple(
    'manifest_entry', [
        'group',
        'target',
        'source',
        'manifest',
    ])


def format_manifest_entry(entry):
    """Convert a manifest_entry instance to its final text representation."""
    return entry.target + '=' + entry.source


def format_manifest_file(manifest):
    """Convert a list of manifest_entry instances to a ZBI manifest file."""
    return ''.join(format_manifest_entry(entry) + '\n' for entry in manifest)


def read_manifest_lines(sep, lines, title, manifest_cwd, result_cwd):
    """Convert an input manifest into a manifest_entry iteration.

    Args:
      sep: Separator used between target and source path (e.g. '=').
      lines: Iterable of ZBI manifest input lines.
      title: Path to the input manifest these lines belong to.
      manifest_cwd: Current source directory assumed by input manifest.
      result_cwd: Current source directory assumed by the resulting entries.

    Returns:
      An iterable of manifest_entry instances. Where 'source' will be relative
      to `result_cwd`.
    """
    for line in lines:
        # Remove the trailing newline.
        assert line.endswith('\n'), 'Unterminated manifest line: %r' % line
        line = line[:-1]

        # The {group}target=source syntax is no longer supported, but just
        # assert that we do not find it in input manifests anymore.
        assert not line.startswith('{'), (
            '{group} syntax no longer supported in manifest line: %r'% line)

        # Grok target=source syntax.
        [target_file, build_file] = line.split(sep, 1)

        if manifest_cwd != result_cwd:
            # Expand the path based on the cwd presumed in the manifest.
            build_file = os.path.normpath(
                os.path.join(manifest_cwd, build_file))
            # Make it relative to the cwd we want to work from.
            build_file = os.path.relpath(build_file, result_cwd)

        yield manifest_entry(None, target_file, build_file, title)


def ingest_manifest_lines(
        sep, lines, title, in_cwd, select_entries, out_cwd, output_group):
    """Convert an input manifest into two lists of selected and unselected entries.

    Args:
      sep: Separator used between target and source path (e.g. '=').
      lines: Iterable of manifest input lines.
      title: Path to the input manifest these lines belong to.
      in_cwd: Current directory assumed by input manifest entries.
      select_entries: A boolean indicating whether to select these manifest
        entries.
      out_cwd: Current directory assumed by the output.
      output_group: A manifest_entry.group value that is only applied to
        selected entries.

    Returns:
      A (selected, unselected) tuple, where each item is a list of manifest_entry
      instances from the input. If |select_entries| is true, |selected| will contain
      all entries from the input, with their group set to |output_group| and
      |unselected| will be empty. If |select_entries| is False, then |selected| will
      be empty, and |unselected| will contain all entries from the input, with their
      group set to None.
    """
    selected = []
    unselected = []
    for entry in read_manifest_lines(sep, lines, title, in_cwd, out_cwd):
        if select_entries:
            selected.append(entry._replace(group=output_group))
        else:
            unselected.append(entry._replace(group=None))

    return selected, unselected


def apply_rewrites(sep, rewrites, entry):
    """Rewrite a manifest entry based on a list of rewrite rules.

    The rewrite rules must be passed as a list of (pattern, line)
    tuples, where |pattern| is an fnmatch pattern, that will be checked
    against the entry's target path.

    In case of a match, the whole entry is replaced by the content of
    |line|, which should typically look like "<target>=<source>" and will be
    parsed through read_manifest_lines().

    The {source} and {target} substitutions are supported in |line|.

    Note that rewrites preserve the original entry's manifest and group values.

    Args:
      sep: Separator used between target and source path (e.g. '=')
      rewrites: A list of (pattern, line) tuples.
      entry: The entry to rewrite if necessary.

    Returns:
      The new entry value after potential rewrites.
    """
    for pattern, line in rewrites:
        if fnmatch.fnmatchcase(entry.target, pattern):
            [new_entry] = read_manifest_lines(
                sep, [line.format(**entry._asdict()) + '\n'], entry.manifest,
                os.path.dirname(entry.manifest),
                os.path.dirname(entry.manifest))
            entry = new_entry._replace(group=entry.group)
    return entry


def contents_entry(entry):
    """Replace a manifest_entry source path with its file content.

    Used to implement the --content option. In a nutshell, an entry
    that looks like '<target>=<source>' will be rewritten to
    '<target>=<content of source file>', preserving other fields
    in the entry.
    """
    with open(entry.source) as file:
        [line] = file.read().splitlines()
        return entry._replace(source=line)


class input_action_base(argparse.Action):
    """Helper base class used to implement --manifest and --entry parsing.

    This is a base class that assumes each derived class provides a
    get_manifest_lines() method returning a (selected, unselected) pair
    of manifest_entry lists, as returned by ingest_manifest_lines().

    This maintains the state of the command-line parser, and creates and
    updates the args.selected and args.unselected lists, described in the
    technical note at the top of this file.
    """
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

        # Only select manifest entries for output if at least one
        # --output=FILE argument was found.
        select_entries = outputs is not None

        cwd = getattr(namespace, 'cwd', '')

        if outputs is not None:
            output_group = len(outputs) - 1
        else:
            output_group = None

        selected, unselected = self.get_manifest_lines(
            namespace, values, cwd, select_entries, namespace.output_cwd, output_group)

        include = getattr(namespace, 'include', [])
        exclude = getattr(namespace, 'exclude', [])
        if include or exclude:

            def included(entry):

                def matches(file, patterns):
                    return any(
                        fnmatch.fnmatch(file, pattern) for pattern in patterns)

                if matches(entry.target, exclude):
                    return False
                if include and not matches(entry.target, include):
                    return False
                return True

            unselected += [entry for entry in selected if not included(entry)]
            selected = list(filter(included, selected))
 
        if getattr(namespace, 'contents', False):
            selected = list(map(contents_entry, selected))
            unselected = list(map(contents_entry, unselected))

        sep = getattr(namespace, 'separator', '=')
        rewrites = [
            entry.split('=', 1) for entry in getattr(namespace, 'rewrite', [])
        ]
        selected = [apply_rewrites(sep, rewrites, entry) for entry in selected]
        unselected = [
            apply_rewrites(sep, rewrites, entry) for entry in unselected
        ]

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
            return ingest_manifest_lines(
                getattr(namespace, 'separator', '='), file, file.name, *args)


class input_entry_action(input_action_base):

    def __init__(self, *args, **kwargs):
        super(input_entry_action, self).__init__(*args, **kwargs)

    def get_manifest_lines(self, namespace, entry, *args):
        return ingest_manifest_lines(
            getattr(namespace, 'separator', '='), [entry + '\n'],
            namespace.entry_manifest, *args)


def common_parse_args(parser):
    """Add common parsier arguments for this script and users of this module.

    See technical note above to understand what these do.
    """
    parser.fromfile_prefix_chars = '@'
    parser.convert_arg_line_to_args = shlex.split
    parser.add_argument(
        '--output',
        action='append',
        required=True,
        metavar='FILE',
        help='Specift next output manifest file.')
    parser.add_argument(
        '--output-cwd',
        default='',
        metavar='DIRECTORY',
        help='Change the current source directory used when writing entries to output files.')
    parser.add_argument(
        '--absolute',
        action='store_true',
        default=False,
        help='Output source file names as absolute paths.')
    parser.add_argument(
        '--cwd',
        default='',
        metavar='DIRECTORY',
        help='Change the current source directory used when reading input entries.')
    parser.add_argument(
        '--manifest',
        action=input_manifest_action,
        metavar='FILE',
        default=[],
        help='Add all entries from input manifest file (must exist)')
    parser.add_argument(
        '--entry',
        action=input_entry_action,
        metavar='PATH=FILE',
        help='Add a single entry as if from an input manifest. Requires a previous ' +
             '--entry-manifest argument.')
    parser.add_argument(
        '--entry-manifest',
        default='<command-line --entry>',
        metavar='TITLE',
        help='Title in lieu of manifest file name for subsequent --entry arguments.')
    parser.add_argument(
        '--include',
        action='append',
        default=[],
        metavar='TARGET',
        help='Only include input entries whose target path matches the fnmatch pattern ' +
             'TARGET. Can be used multiple times to extend the list of patterns. These ' +
             'are always applied after --exclude pattern exclusions.')
    parser.add_argument(
        '--reset-include',
        action='store_const',
        const=[],
        dest='include',
        help='Reset the --include pattern list to be empty.')
    parser.add_argument(
        '--exclude',
        action='append',
        default=[],
        metavar='TARGET',
        help='Ignore input entries whose target path matches the fnmatch pattern TARGET. ' +
             'Can be used multiple times to extend the list of patterns.'),
    parser.add_argument(
        '--reset-exclude',
        action='store_const',
        const=[],
        dest='exclude',
        help='Reset the --exclude pattern list to be empty.')
    parser.add_argument(
        '--separator',
        default='=',
        metavar='SEP',
        help='Use SEP between TARGET and SOURCE in manifet entries.')
    return parser.parse_args()


def parse_args():
    parser = argparse.ArgumentParser(description='Read manifest files.')
    parser.add_argument(
        '--copy-contentaddr',
        action='store_true',
        default=False,
        help='Copy to content-addressed targets, not manifest.')
    parser.add_argument(
        '--sources',
        action='store_true',
        default=False,
        help='Write source file per line, not manifest entry.')
    parser.add_argument(
        '--contents',
        action='store_true',
        default=False,
        help='Replace each source file name with its contents.')
    parser.add_argument(
        '--no-contents',
        action='store_false',
        dest='contents',
        help='Reset previous --contents')
    parser.add_argument(
        '--rewrite',
        action='append',
        default=[],
        metavar='PATTERN=ENTRY',
        help='Replace entries whose target matches PATTERN with ENTRY,'
        ' which can use {source} and {target} substitutions.'),
    parser.add_argument(
        '--reset-rewrite',
        dest='rewrite',
        action='store_const',
        const=[],
        help='Reset previous --rewrite.')
    parser.add_argument(
        '--unique',
        action='store_true',
        default=False,
        help='Elide duplicates even with different sources.')
    parser.add_argument(
        '--stamp', metavar='FILE', help='Touch FILE at the end.')
    args = common_parse_args(parser)
    if args.copy_contentaddr:
        if args.contents:
            parser.error('--copy-contentaddr is incompatible with --contents')
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
            line = entry.target + args.separator + line
        if args.unique:
            output_sets[entry.group][entry.target] = line
        else:
            output_sets[entry.group].add(line)
    for output_filename, output_set in zip(args.output, output_sets):
        if args.copy_contentaddr:
            created_dirs = set()
            for target, source in output_set.items():
                target_path = os.path.join(output_filename, target)
                if os.path.exists(target_path):
                    continue
                target_dir = os.path.dirname(target_path)
                if target_dir not in created_dirs:
                    if not os.path.exists(target_dir):
                        os.makedirs(target_dir)
                    created_dirs.add(target_dir)
                shutil.copyfile(source, target_path)
        else:
            with open(output_filename, 'w') as file:
                file.write(
                    ''.join(
                        sorted(
                            line + '\n' for line in (
                                iter(output_set.values()) if args.
                                unique else output_set))))
    if args.stamp:
        with open(args.stamp, 'w') as file:
            os.utime(file.name, None)
    return 0


if __name__ == '__main__':
    sys.exit(main())
