#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys
from pathlib import Path
from string import Template

FUCHSIA_DIR = Path(os.environ["FUCHSIA_DIR"])
ERRCAT_INDEX_FILE_PATH = FUCHSIA_DIR / "docs/reference/fidl/language/errcat.md"
ERRCAT_DIR_PATH = FUCHSIA_DIR / "docs/reference/fidl/language/error-catalog"
TEMPLATE_PATH = FUCHSIA_DIR / "tools/fidl/scripts/add_errcat_template.md"
FIDLC_DIAGNOSTICS_FILE_PATH = FUCHSIA_DIR / "tools/fidl/fidlc/include/fidl/diagnostics.h"

REGEX_INDEX_ENTRY = '^<<error-catalog\/_fi-(\d+)\.md>>'


def find_line_num(regex, line):
    result = re.search(regex, line)
    if result is None:
        return -1
    return int(result.groups(1)[0])


def insert_lines_at_line_num(path, lines, line_num, line):
    lines.insert(line_num, line + "\n")
    with open(path, "wt") as f:
        f.write("".join(lines))


def insert_entry(path, entry_matcher, num, insert):
    # Only add an entry for this numeral if none already exists.
    already_exists = False
    with open(path, 'r') as f:
        lines = f.readlines()

    # Do a pass over the lines in the file to ensure that this entry does not already exist.
    already_exists = any(line.startswith(insert) for line in lines)

    # If we do need a file do another pass, inserting the supplied text in the correct position.
    if not already_exists:
        insert_before = None
        for line_num in range(len(lines)):
            line = lines[line_num]
            entry_num = find_line_num(entry_matcher, line)
            if entry_num == -1:
                continue

            insert_before = line_num
            if entry_num > num:
                if insert_before is None:
                    insert_before = line_num
                insert_lines_at_line_num(path, lines, insert_before, insert)
                insert_before = None
                break
            else:
                insert_before = line_num

        # Handle the edge case of the entry needing to be placed at the end of the list.
        if insert_before is not None:
            insert_lines_at_line_num(path, lines, insert_before + 2, insert)

    return not already_exists


def main(args):
    """Given an error numeral for the `fi-` domain, create a new markdown file to describe that
    error, or add it to the error catalog listing in errcat.md, or flip its entry in diagnostics.h
    from an `Undocumented[Error|Warning]Def` specialization to a documented one, or any combination
    thereof.

    Usage example:
    tools/fidl/scripts/add_errcat_entry.py 123
    tools/fidl/scripts/add_errcat_entry.py 1
    tools/fidl/scripts/add_errcat_entry.py 1000
    """

    # Create the error id string, taking care to appropriately inject leading zeroes.
    n = args.numeral
    ns = "%04d" % args.numeral

    # Only add an index entry for this numeral if none exists.
    entry = "<<error-catalog/_fi-%s.md>>\n" % ns
    index_written = insert_entry(
        ERRCAT_INDEX_FILE_PATH, REGEX_INDEX_ENTRY, n, entry)

    # Only add a markdown file for this numeral if none exists.
    file_created = True
    markdown_file = ERRCAT_DIR_PATH / ("_fi-%s.md" % ns)
    if markdown_file.is_file():
        file_created = False
    else:
        # Create the file for this errcat entry, substituting the proper numeral along the way.
        with open(TEMPLATE_PATH, 'r') as f:
            template = Template(f.read())
            with open(markdown_file, 'wt') as f:
                f.write(template.substitute(num=ns))

    # Replace the specialization used in diagnostics.h, if the error already exists in that file.
    spec_swapped = False
    with open(FIDLC_DIAGNOSTICS_FILE_PATH, 'rt') as f:
        text = f.read()

        # Replace the specialization with one that is no longer `Undocumented*`.
        (new_text, count) = re.subn(
            "(constexpr )(Undocumented)((?:Error|Warning)Def<%d[,>])" % n,
            r'\1\3',
            text,
            count=1)
        if count == 1:
            spec_swapped = True
        with open(FIDLC_DIAGNOSTICS_FILE_PATH, "wt") as f:
            f.write(new_text)

    # Tell the user what a great job we've done.
    if not file_created and not index_written and not spec_swapped:
        print(
            "There is already an index entry and a markdown for %s, nothing to do."
            % ns)
        return 0
    if file_created:
        print(
            "Added new markdown file for fi-%s at %s/_fi-%s.md." %
            (ns, ERRCAT_DIR_PATH, ns))
    if index_written:
        print("Added new entry for fi-%s to %s." % (ns, ERRCAT_INDEX_FILE_PATH))
    if spec_swapped:
        print(
            "The DiagnosticDef specialization for fi-%s in %s now prints a permalink."
            % (ns, FIDLC_DIAGNOSTICS_FILE_PATH))

    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Add an entry to //docs/references/fidl/language/errcat.md')
    parser.add_argument(
        'numeral',
        metavar='N',
        type=int,
        help='The numeral of the error being added to the errcat')
    main(parser.parse_args())
