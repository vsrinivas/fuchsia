#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys
import textwrap
from pathlib import Path
from string import Template

FUCHSIA_DIR = Path(os.environ["FUCHSIA_DIR"])

# Paths for relevant docs.
ERRCAT_INDEX_FILE_PATH = FUCHSIA_DIR / "docs/reference/fidl/language/errcat.md"
REDIRECT_FILE_PATH = FUCHSIA_DIR / "docs/error/_redirects.yaml"
DOCS_DIR_PATH = FUCHSIA_DIR / "docs/reference/fidl/language/error-catalog"
DOC_TEMPLATE_PATH = FUCHSIA_DIR / "tools/fidl/scripts/add_errcat_templates/doc.md"

# Paths for relevant fidc locations.
FIDLC_DIAGNOSTICS_FILE_PATH = FUCHSIA_DIR / "tools/fidl/fidlc/include/fidl/diagnostics.h"
FIDLC_ERRCAT_TESTS_FILE_PATH = FUCHSIA_DIR / "tools/fidl/fidlc/tests/errcat_good_tests.cc"
FIDLC_BUILD_GN_FILE_PATH = FUCHSIA_DIR / "tools/fidl/fidlc/tests/BUILD.gn"
FIDLC_BAD_TESTS_DIR_PATH = FUCHSIA_DIR / "tools/fidl/fidlc/tests/fidl/bad"
FIDLC_BAD_TEMPLATE_PATH = FUCHSIA_DIR / "tools/fidl/scripts/add_errcat_templates/bad.md"
FIDLC_GOOD_TESTS_DIR_PATH = FUCHSIA_DIR / "tools/fidl/fidlc/tests/fidl/good"
FIDLC_GOOD_TEMPLATE_PATH = FUCHSIA_DIR / "tools/fidl/scripts/add_errcat_templates/good.md"

REGEX_INDEX_ENTRY = '^<<error-catalog\/_fi-(\d+)\.md>>'
REGEX_REDIRECT_ENTRY = '^- from: \/fuchsia-src\/error\/fi-(\d+)'
REGEX_BAD_TEST_ENTRY = '\s*"fidl\/bad\/fi-(\d+)\.test.fidl",'
REGEX_GOOD_TEST_ENTRY = '\s*"fidl\/good\/fi-(\d+)\.test.fidl",'
REGEX_ERRCAT_TEST_FILE_ENTRY = 'TEST\(ErrcatTests, Good(\d+)'


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
    already_exists = any(
        line.startswith(insert.split('\n')[0]) for line in lines)

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


def create_file(file_path, template_path, numeral):
    # Only add this kind of file for this numeral if none exists.
    if file_path.is_file():
        return False

    # Create the file for this errcat entry, substituting the proper numeral along the way.
    with open(template_path, 'r') as f:
        template = Template(f.read())
        with open(file_path, 'wt') as f:
            f.write(template.substitute(num=numeral))
            return True


def main(args):
    """Given an error numeral for the `fi-` domain, create a new markdown file to describe that
    error, add it to the error catalog listing in errcat.md, add it to the redirect list in
    _redirects.yaml, flip its entry in diagnostics.h from an `Undocumented[Error|Warning]Def`
    specialization to a documented one, or any combination thereof.

    Usage example:
    tools/fidl/scripts/add_errcat_entry.py 123
    tools/fidl/scripts/add_errcat_entry.py 1
    tools/fidl/scripts/add_errcat_entry.py 1000
    """

    progress = {
        'index_entry_added': False,
        'redirect_entry_added': False,
        'markdown_file_created': False,
        'bad_test_added': False,
        'good_test_added': False,
        'test_case_added': False,
        'diagnostic_specialization_swapped': False,
    }

    # Create the error id string, taking care to appropriately inject leading zeroes.
    n = args.numeral
    ns = "%04d" % args.numeral

    # Only add an index entry for this numeral if none exists.
    progress['index_entry_added'] = insert_entry(
        ERRCAT_INDEX_FILE_PATH, REGEX_INDEX_ENTRY, n,
        "<<error-catalog/_fi-%s.md>>\n" % ns)

    # Only add a redirect entry for this numeral if none exists.
    progress['redirect_entry_added'] = insert_entry(
        REDIRECT_FILE_PATH, REGEX_REDIRECT_ENTRY, n,
        '- from: /fuchsia-src/error/fi-%s\n  to: /fuchsia-src/reference/fidl/language/errcat.md#fi-%s'
        % (ns, ns))

    # Insert the good and bad tests into the BUILD.gn file. Yes, this does read/write the file twice
    # consecutively, but that is okay.
    progress['bad_test_added'] = insert_entry(
        FIDLC_BUILD_GN_FILE_PATH, REGEX_BAD_TEST_ENTRY, n,
        '    "fidl/bad/fi-%s.test.fidl",' % ns)
    progress['good_test_added'] = insert_entry(
        FIDLC_BUILD_GN_FILE_PATH, REGEX_GOOD_TEST_ENTRY, n,
        '    "fidl/good/fi-%s.test.fidl",' % ns)

    # Only add a test entry for the good test case if none exists. This will insert tests in
    # alphabetical order, which is not ideal, but fine for the time being.
    insert = textwrap.dedent(
        """TEST(ErrcatTests, Good%s) {
  TestLibrary library;
  library.AddFile("good/fi-%s.test.fidl");
  ASSERT_COMPILED(library);
}
""") % (ns, ns)
    progress['test_case_added'] = insert_entry(
        FIDLC_ERRCAT_TESTS_FILE_PATH, REGEX_ERRCAT_TEST_FILE_ENTRY, n, insert)

    # Replace the specialization used in diagnostics.h, if the error already exists in that file.
    with open(FIDLC_DIAGNOSTICS_FILE_PATH, 'rt') as f:
        text = f.read()

        # Replace the specialization with one that is no longer `Undocumented*`.
        (new_text, count) = re.subn(
            "(constexpr )(Undocumented)((?:Error|Warning)Def<%d[,>])" % n,
            r'\1\3',
            text,
            count=1)
        if count == 1:
            progress['diagnostic_specialization_swapped'] = True
        with open(FIDLC_DIAGNOSTICS_FILE_PATH, "wt") as f:
            f.write(new_text)

    # Add any new files we need.
    progress['markdown_file_created'] = create_file(
        DOCS_DIR_PATH / ("_fi-%s.md" % ns), DOC_TEMPLATE_PATH, ns)
    progress['bad_test_created'] = create_file(
        FIDLC_BAD_TESTS_DIR_PATH / ("fi-%s.test.fidl" % ns),
        FIDLC_BAD_TEMPLATE_PATH, ns)
    progress['good_test_created'] = create_file(
        FIDLC_GOOD_TESTS_DIR_PATH / ("fi-%s.test.fidl" % ns),
        FIDLC_GOOD_TEMPLATE_PATH, ns)

    # Tell the user what a great job we've done.
    print()
    if all(action is False for action in list(progress.values())):
        print(
            "There is nothing to do for %s - perhaps you've already run this command, or someone else did this error already?\n"
            % ns)
        return 0
    if progress['index_entry_added']:
        print(
            "  * Added new index entry for fi-%s to %s." %
            (ns, ERRCAT_INDEX_FILE_PATH))
    if progress['redirect_entry_added']:
        print(
            "  * Added new redirect entry for fi-%s to %s." %
            (ns, REDIRECT_FILE_PATH))
    if progress['bad_test_added']:
        print(
            "  * A BUILD rule for the bad test case for fi-%s has been added to %s."
            % (ns, FIDLC_BUILD_GN_FILE_PATH))
    if progress['good_test_added']:
        print(
            "  * A BUILD rule for the good test case for fi-%s has been added to %s."
            % (ns, FIDLC_BUILD_GN_FILE_PATH))
    if progress['test_case_added']:
        print(
            "  * A good test case for fi-%s has been added to %s." %
            (ns, FIDLC_ERRCAT_TESTS_FILE_PATH))
    if progress['diagnostic_specialization_swapped']:
        print(
            "  * The DiagnosticDef specialization for fi-%s in %s now prints a permalink."
            % (ns, FIDLC_DIAGNOSTICS_FILE_PATH))
    if progress['markdown_file_created']:
        print(
            "  * Added new markdown file for fi-%s at %s/_fi-%s.md." %
            (ns, DOCS_DIR_PATH, ns))

    # Add a line break, plus a horizontal rule, to emphasize the actionable reports.
    if progress['bad_test_created'] or progress['good_test_created']:
        print(
            "\n----------------------------------------------------------------------\n"
        )
    if progress['bad_test_created']:
        print(
            "  * Added bad example for fi-%s at %s/fi-%s.test.fidl. Please resolve the TODO(DONOTSUBMIT)!"
            % (ns, DOCS_DIR_PATH, ns))
    if progress['good_test_created']:
        print(
            "  * Added bad example for fi-%s at %s/fi-%s.test.fidl. Please resolve the TODO(DONOTSUBMIT)!"
            % (ns, DOCS_DIR_PATH, ns))
    print()

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
