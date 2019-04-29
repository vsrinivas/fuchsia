#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys
from typing import List, Tuple, Set, DefaultDict, Dict, Callable, Optional
from collections import defaultdict

from difl.changes import Change
from difl.ir import Libraries, Library, Declaration
from difl.library import library_changes

# All the changes for a given line
LineChanges = Tuple[int, Set[str]]
# All the changes for a given source file
FileChanges = List[LineChanges]
# All the changes in to all the source files in a library
LibraryChanges = Dict[str, FileChanges]

# Regex to match expected changes in test source files
EXPECT_RE = re.compile(r'\s*//!(.+)')


def expected_file_changes(filename: str) -> FileChanges:
    lines = [l.rstrip() for l in open(filename).readlines()]
    expected_changes: FileChanges = []
    changes: Set[str] = set()
    for num, line in enumerate(lines):
        match = EXPECT_RE.match(line)
        if match:
            changes.add(match.group(1))
        elif len(changes):
            expected_changes.append((num + 1, changes))
            changes = set()
    assert not len(changes)
    return expected_changes


def expected_library_changes(library: Library,
                             source_base_dir: str) -> LibraryChanges:
    '''Extract the list of expected changes from the source files for the library based on //! lines'''
    return {
        filename: expected_file_changes(
            os.path.join(source_base_dir, filename))
        for filename in library.filenames
    }


# A function that returns either the before or after Declaration from a Change
DeclSelector = Callable[[Change], Optional[Declaration]]


def before_selector(change: Change) -> Optional[Declaration]:
    return change.before


def after_selector(change: Change) -> Optional[Declaration]:
    return change.after


def actual_file_changes(changes: List[Change], filename: str,
                        decl_selector: DeclSelector) -> FileChanges:
    line_changes_dict: DefaultDict[int, Set[str]] = defaultdict(set)
    for change in changes:
        decl = decl_selector(change)
        if decl is None: continue
        if decl.filename != filename: continue
        line_changes_dict[decl.line].add(change.name)
    line_changes: List[Tuple[int, Set[str]]] = list(line_changes_dict.items())
    line_changes.sort(key=lambda t: t[0])
    return line_changes


def actual_library_changes(library: Library, changes: List[Change],
                           decl_selector: DeclSelector) -> LibraryChanges:
    return {
        filename: actual_file_changes(changes, filename, decl_selector)
        for filename in library.filenames
    }


def describe_file_differences(filename: str, expected: FileChanges,
                              actual: FileChanges):
    # dictionaries mapping line numbers to change descriptions
    expected_lines = {line: changes for line, changes in expected}
    actual_lines = {line: changes for line, changes in actual}
    # union of the line numbers from expected and actual
    line_numbers: Set[int] = set(
        [line for line, _ in expected] + [line for line, _ in actual])
    for num in sorted(line_numbers):
        expected_changes = expected_lines.get(num, set())
        actual_changes = actual_lines.get(num, set())
        if expected_changes == actual_changes: continue
        print('%s:%d' % (filename, num))
        for c in actual_changes - expected_changes:
            print('  unexpected %s' % c)
        for c in expected_changes - actual_changes:
            print('  expected %s' % c)
        print()


def describe_library_differences(expected: LibraryChanges,
                                 actual: LibraryChanges):
    # the union of the filenames in expected and actual
    filenames = set(list(expected.keys()) + list(actual.keys()))
    for filename in sorted(filenames):
        describe_file_differences(filename, expected.get(filename, []),
                                  actual.get(filename, []))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--build-dir', help='Fuchsia build directory', required=True)
    parser.add_argument(
        '--before',
        help="Path to JSON IR for before version of the library",
        required=True)
    parser.add_argument(
        '--after',
        help="Path to JSON IR for after version of the library",
        required=True)
    parser.add_argument(
        '--stamp', help="Path to stamp file to write after completion")
    args = parser.parse_args()
    fidl_json_dir = os.path.join(
        args.build_dir, 'gen/garnet/public/lib/fidl/tools/difl_test_fidl')

    before = Libraries().load(args.before)
    after = Libraries().load(args.after)

    before_expected_changes = expected_library_changes(before, args.build_dir)
    after_expected_changes = expected_library_changes(after, args.build_dir)

    identifier_compatibility: Dict[str, bool] = {}

    changes: List[Change] = library_changes(before, after, identifier_compatibility)

    before_actual_changes = actual_library_changes(before, changes,
                                                   before_selector)
    after_actual_changes = actual_library_changes(after, changes,
                                                  after_selector)

    describe_library_differences(before_expected_changes,
                                 before_actual_changes)
    describe_library_differences(after_expected_changes, after_actual_changes)

    if before_expected_changes == before_actual_changes and after_expected_changes == after_actual_changes:
        if args.stamp:
            with open(args.stamp, 'w') as stamp:
                stamp.truncate()
    else:
        sys.exit(1)
