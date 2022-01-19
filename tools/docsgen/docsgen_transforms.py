#!/usr/bin/env fuchsia-vendored-python
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Transform references docs and regular docs for fuchsia.dev.

This script implements a number of different transforms to 
prepare reference docs and regular docs for doc_checker, 
mdlint and ultimately fuchsia.dev.
"""

import glob
import os
import re

ORIGIN_USER_FRIENDLY_URI = "https://cs.opensource.google/fuchsia/fuchsia/+/main:"
ORIGIN_EDIT_SOURCE_URI = "https://ci.android.com/edit?repo=fuchsia/fuchsia/main&file="

REPORT_BUG_URI = (
    "https://bugs.fuchsia.dev/p/fuchsia/issues/entry?"
    "template=Fuchsia.dev%20Documentation&"
    "description=Issue%20on%20page:%20"
    "https://ci.android.com/edit?repo=fuchsia/fuchsia/main%26file=")

HEADER_DELIMETER = "<!-- The header above is automatically added to this file. Do not modify anything above this line. -->"
FOOTER_DELIMETER = "<!-- The footer below is automatically added to this file. Do not modify anything below this line. -->"

HEADER_INCLUDED_DOCS = "<!-- the content below is included from: {original_file} -->"
FOOTER_INCLUDED_DOCS = "<!-- end of included content from: {original_file} -->"

REFERENCEDOCS_USER_FRIENDLY_SOURCE_URI = "https://fuchsia.googlesource.com/reference-docs/+show/main/"
REFERENCEDOCS_EDIT_SOURCE_URI = REFERENCEDOCS_USER_FRIENDLY_SOURCE_URI

HEADER_REFERENCE_DOCS = "<!-- public markdown source of this page: {original_file} -->"
FOOTER_REFERENCE_DOCS = ""

METADATA_HEADER = """Project: /_project.yaml
Book: /_book.yaml
edit_url: {edit_original_file}
bug_url: {file_doc_bug}
"""
HEADER_REGULAR_DOCS = "<!-- original source of this page: {original_file} -->"

FOOTER_REGULAR_DOCS = '''<div class="align-right">
<a href="{file_doc_bug}" title="Report a bug or suggest improvements on this page"><span class="material-icons" style="font-size: 18px">bug_report</span></a>
<a href="{original_file}" title="View the source code of this page"><span class="material-icons" style="font-size: 18px">code</span></a>
<a href="{edit_original_file}" title="Edit the source code of this page"><span class="material-icons" style="font-size: 18px">edit</span></a>
</div>
'''

REFERENCEDOCS_OUT = "reference/"

FILES_AT_ROOT = [
    "CONTRIBUTING.md",
    "CODE_OF_CONDUCT.md",
]


def _add_markdown_header_and_footer(is_regular_docs):
    if is_regular_docs:
        root_url = ORIGIN_USER_FRIENDLY_URI
        edit_root_url = ORIGIN_EDIT_SOURCE_URI
        header = HEADER_REGULAR_DOCS
        footer = FOOTER_REGULAR_DOCS
    else:
        root_url = REFERENCEDOCS_USER_FRIENDLY_SOURCE_URI
        edit_root_url = REFERENCEDOCS_EDIT_SOURCE_URI
        header = HEADER_REFERENCE_DOCS
        footer = FOOTER_REFERENCE_DOCS

    for f in glob.glob("**.md"):
        matching_file = open(f, "r")
        file_contents = matching_file.read()
        matching_file.close()
        original_file = root_url + f
        edit_original_file = edit_root_url + f
        file_doc_bug = REPORT_BUG_URI + f

        # markdown files starting with "_" can't have metadata or footer
        if f.startswith("_"):
            actual_h = HEADER_INCLUDED_DOCS.format(original_file=original_file)
            actual_f = FOOTER_INCLUDED_DOCS.format(original_file=original_file)
        else:
            actual_h = f'{METADATA_HEADER.format(edit_original_file=edit_original_file, file_doc_bug=file_doc_bug)}{header.format(original_file=original_file)}'
            actual_f = footer.format(
                original_file=original_file,
                edit_original_file=edit_original_file,
                file_doc_bug=file_doc_bug)

        with open(f, "w") as updated_file:
            updated_file.write(
                f"{actual_h}\n{HEADER_DELIMETER}\n{file_contents}\n{FOOTER_DELIMETER}\n{actual_f}\n"
            )


def _remove_toc():
    for f in glob.glob("**.md"):
        matching_file = open(f, "r")
        file_contents = matching_file.read()
        matching_file.close()
        mod_file_contents = re.sub(
            r"\[TOC\]", "<!-- commented [TOC] -->", file_contents, 1)
        if file_contents != mod_file_contents:
            with open(f, "w") as updated_file:
                updated_file.write(mod_file_contents)


# Rename README.md to index.md
def _rename_readme():
    glob_patterns = ["README.md", "**/README.md"]
    new_filename = "index.md"
    files_grabbed = []
    for pattern in glob_patterns:
        files_grabbed.extend(glob.glob(pattern))
    for file_path in files_grabbed:
        new_path = os.path.join(os.path.dirname(file_path), new_filename)
        os.rename(file_path, new_path)


def _canonical_header_anchorid():
    for f in glob.glob("**.md"):
        matching_file = open(f, "r")
        file_contents = matching_file.read()
        matching_file.close()
        mod_file_contents = re.sub(
            r"(^#+\s+)(.*){#([^\s{}]+)([^}]*)",
            r'\1\2{:#\3\4 transformation="converted"',
            file_contents,
            flags=re.MULTILINE)
        if file_contents != mod_file_contents:
            with open(f, "w") as updated_file:
                updated_file.write(mod_file_contents)


def _transform_toc_files(is_regular_docs):
    for f in glob.glob("**/_toc.yaml"):
        matching_file = open(f, "r")
        file_lines = matching_file.readlines()
        matching_file.close()
        modified = False
        for index, line in enumerate(file_lines):
            if not re.match(r'.*https://.*', line) and not re.match(
                    r'.*http://.*', line):
                line, sub_amount = re.subn(
                    '(^\\s*-?\\s+(path|include)\\s*:\\s*.*)/README.md(\\s*$)',
                    r'\1/index.md\3', line)
                if is_regular_docs:
                    line, s1 = re.subn(
                        f'(^\\s*-?\\s+(path|include)\\s*:\\s*)/docs((/.*)?\\s*$)',
                        r'\1/fuchsia-src\3', line)
                    root_files = "|".join(FILES_AT_ROOT)
                    line, s2 = re.subn(
                        f'(^\\s*-?\\s+(path|include)\\s*:\\s*)/({root_files})(\\s*$)',
                        r'\1/fuchsia-src/\3\4', line)
                    sub_amount += s1 + s2
                if sub_amount > 0:
                    file_lines[index] = line
                    if not modified:
                        modified = True
        if modified:
            with open(f, "w") as updated_file:
                updated_file.writelines(file_lines)


# TODO (https://fxbug.dev/91652) To refactor and streamline
def referencedocs_transforms():
    _add_markdown_header_and_footer(False)
    _remove_toc()
    _canonical_header_anchorid()
    _rename_readme()
    os.rename("tools", os.join(REFERENCEDOCS_OUT, "tools"))
    os.rename("sdk", os.join(REFERENCEDOCS_OUT, "tools"))
    _transform_toc_files(False)
