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


def get_index_entry_numeral(line):
    result = re.search('^<<error-catalog\/_fi-(\d+)\.md>>', line)
    if result is None:
        return -1
    return int(result.groups(1)[0])


def insert_index_entry(lines, line_num, line):
    lines.insert(line_num, line + "\n\n")
    with open(ERRCAT_INDEX_FILE_PATH, "wt") as f:
        f.write("".join(lines))


def main(args):
    """Given an error numeral for the `fi-` domain, create a new markdown file to describe that error,
    or add it to the error catalog listing in errcat.md, or both.

    Usage example:
    tools/fidl/scripts/add_errcat_entry.py 123
    tools/fidl/scripts/add_errcat_entry.py 1
    tools/fidl/scripts/add_errcat_entry.py 1000
    """

    # Create the search string (needle), taking care to appropriately inject leading zeroes.
    n = args.numeral
    ns = "%04d" % args.numeral
    needle = "<<error-catalog/_fi-%s.md>>" % ns

    # Only add an index entry for this numeral if none exists.
    index_exists = False
    with open(ERRCAT_INDEX_FILE_PATH, 'r') as f:
        lines = f.readlines()

        # Do a pass over the lines in the file to ensure that this entry does not already exist.
        index_exists = any(line.startswith(needle) for line in lines)

        # If we do need a file do another pass, inserting the needle in the correct position.
        if not index_exists:
            insert_before = None
            for line_num in range(len(lines)):
                line = lines[line_num]
                entry_num = get_index_entry_numeral(line)
                if entry_num == -1:
                    continue

                insert_before = line_num
                if entry_num > n:
                    if insert_before is None:
                        insert_before = line_num
                    insert_index_entry(lines, insert_before, needle)
                    insert_before = None
                    break
                else:
                    insert_before = line_num

            # Handle the edge case of the entry needing to be placed at the end of the list.
            if insert_before is not None:
                insert_index_entry(lines, insert_before + 2, needle)

    # Only add a markdown file for this numeral if none exists.
    file_exists = False
    markdown_file = ERRCAT_DIR_PATH / ("_fi-%s.md" % ns)
    if markdown_file.is_file():
        file_exists = True
    else:
        # Create the file for this errcat entry, substituting the proper numeral along the way.
        with open(TEMPLATE_PATH, 'r') as f:
            template = Template(f.read())
            with open(markdown_file, 'wt') as f:
                f.write(template.substitute(num=ns))

    # Tell the user what a great job we've done.
    if index_exists and file_exists:
        print(
            "There is already an index entry and a markdown for %s, nothing to do."
            % ns)
        return -1
    if not index_exists:
        print("Added new entry for fi-%s to %s." % (ns, ERRCAT_INDEX_FILE_PATH))
    if not file_exists:
        print(
            "Added new markdown file for fi-%s at %s/_fi-%s.md." %
            (ns, ERRCAT_DIR_PATH, ns))

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
