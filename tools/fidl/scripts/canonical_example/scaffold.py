#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys
import textwrap
from pathlib import Path
from string import Template

# Output target paths that may be modified.
FUCHSIA_DIR = Path(os.environ["FUCHSIA_DIR"])
FIDL_DOCS_ROOT_DIR = FUCHSIA_DIR / "docs/development/languages/fidl"
EXAMPLE_CODE_BASE_DIR = FUCHSIA_DIR / "examples/fidl/new"
EXAMPLE_DOCS_BASE_DIR = FIDL_DOCS_ROOT_DIR / "examples"
DOCS_TOC_YAML_FILE = FIDL_DOCS_ROOT_DIR / "_toc.yaml"
DOCS_ROOT_README_FILE = EXAMPLE_DOCS_BASE_DIR / "README.md"
CODE_ROOT_BUILD_GN_FILE = EXAMPLE_CODE_BASE_DIR / "BUILD.gn"

# Template source paths.
TEMPLATES_DIR = FUCHSIA_DIR / "tools/fidl/scripts/canonical_example/templates"
CREATE_CODE_TEMPLATES_DIR = TEMPLATES_DIR / "create_code"
CREATE_DOCS_TEMPLATES_DIR = TEMPLATES_DIR / "create_docs"

# Startup-compiled regexes.
REGEX_IS_PASCAL_CASE = re.compile(r"^(?:[A-Z][a-z0-9]+)+$")
REGEX_IS_SNAKE_CASE = re.compile(r"^[a-z][a-z0-9_]+$")
REGEX_TO_TEXT_CASE = re.compile(r"_+")
REGEX_OPEN_DNCR = re.compile(r"\sDO_NOT_REMOVE_COMMENT")
REGEX_CLOSE_DNCR = re.compile(r"\s/DO_NOT_REMOVE_COMMENT")

# Other important defaults.
BASELINE = "baseline"
DNS = "DO" + "NOT" + "SUBMIT"
DNRC = "DO_NOT_REMOVE_COMMENT"


# Custom exceptions.
class AlreadyExistsError(Exception):
    pass


class InputError(Exception):
    pass


def is_pascal_case(input):
    """Check if a string is PascalCase, aka (upper) CamelCase
    """
    return REGEX_IS_PASCAL_CASE.match(input)


def is_snake_case(input):
    """Check if a string is snake_case.
    """
    return REGEX_IS_SNAKE_CASE.match(input)


def to_camel_case(input):
    """Converts a snake_case string to (lower) camelCase, ex "iAmCamelCase".
    """
    words = input.split('_')
    return words[0] + "".join(word.capitalize() for word in words[1:])


def to_flat_case(input):
    """Converts a snake_case string to (lower) camelCase, ex "iamflatcase".
    """
    return input.replace("_", "")


def to_pascal_case(input):
    """Converts a snake_case string to PascalCase, aka (upper) CamelCase, ex "IAmPascalCase".
    """
    return to_camel_case(input).capitalize()


def to_sentence_case(input):
    """Converts a snake_case string to a sentence case text case string, ex "I am sentence case".
    """
    return to_text_case(input).capitalize()


def to_text_case(input):
    """Converts a snake_case string to an all lowercase text case string, ex "i am text case".
    """
    return re.sub(REGEX_TO_TEXT_CASE, " ", input)


def build_subs(series, variant, protocol, bug):
    """Builds the substitution map.
    """
    if not is_snake_case(series):
        raise InputError("Expected series '%s' to be snake_case" % series)
    if not is_snake_case(variant):
        raise InputError("Expected variant '%s' to be snake_case" % variant)
    if not is_pascal_case(protocol):
        raise InputError("Expected protocol '%s' to be pascal_case" % protocol)
    if not (isinstance(bug, int) and bug > 0):
        raise InputError("Expected bug '%s' to be an unsigned integer" % bug)

    # LINT.IfChange
    return {
        'bug': "%s" % bug,
        'dns': DNS,
        'protocol_pascal_case': protocol,
        'series_flat_case': to_flat_case(series),
        'series_sentence_case': to_sentence_case(series),
        'series_snake_case': series,
        'series_text_case': to_text_case(series),
        'variant_flat_case': to_flat_case(variant),
        'variant_snake_case': variant,
    }
    # LINT.ThenChange(/tools/fidl/scripts/canonical_example/README.md)


def apply_subs(series, subs, source, target):
    """Apply substitutions to templates located in the source dir, and write them to the target dir.

    Returns a dict of paths to numbers, wih the number representing the count of outstanding DNS
    TODOs in the file.
    """
    dns_counts = {}
    for path, subdirs, files in os.walk(source):
        for file_name in files:
            # Process the filename, including doing any substitutions necessary.
            temp_path = os.path.join(path, file_name)
            rel_path = os.path.relpath(
                temp_path[:temp_path.rindex('.')], source)
            out_path = target / series / Template(
                str(rel_path)).substitute(subs)

            # Do substitutions on the file text as well.
            out_contents = ""
            with open(temp_path, 'r') as f:
                out_contents = Template(f.read()).substitute(subs)

            # Count DNS occurrences.
            dns_count = out_contents.count("TODO(%s)" % DNS)
            dns_counts[str(out_path)] = dns_count

            # Do the actual file writes.
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(out_path, "wt") as f:
                f.write(out_contents)

    return dns_counts


def lines_between_dncr_tags(lines, src_path):
    """Take a list of lines, and return the sub-list between the DNCR tags.

    Returns the selected lines, plus the index at which they are located in the source file.
    """
    opened = -1
    for line_num, line in enumerate(lines):
        if opened >= 0:
            if REGEX_CLOSE_DNCR.search(line):
                return opened + 1, line_num
        elif REGEX_OPEN_DNCR.search(line):
            opened = line_num

    if opened < 0:
        raise InputError(
            "Closing /DO_NOT_REMOVE_COMMENT comment missing from '%s'" %
            src_path)
    raise InputError(
        "DO_NOT_REMOVE_COMMENT comment missing from '%s'" % src_path)


def validate_does_not_exist(needle, files):
    """Check to see if a canonical example series already exists anywhere we want to insert it.

    For each validated file, return a three-tuple containing all the lines of the file, the start,
    and end of the range covered by the `DO_NOT_REMOVE_COMMENT` tags.
    """
    ranges = {}
    for path in files:
        # Find `DO_NOT_REMOVE_COMMENT` and `/DO_NOT_REMOVE_COMMENT`
        lines = []
        with open(path, 'r') as f:
            lines = f.readlines()
            start, end = lines_between_dncr_tags(lines, path)

        # Does the needle appear anywhere in between those bounds?
        for line in lines:
            if needle in line:
                raise AlreadyExistsError(
                    "An entry for '%s' already exists in '%s'" % (needle, path))

        ranges[path] = (lines, start, end)
    return ranges


def create_new(name, protocol, bug):
    """Create a new canonical example series.
    """
    # Build the substitution dictionary.
    subs = build_subs(name, BASELINE, protocol, bug)

    # Do a first pass through the files we'll be editing, to ensure that they are in a good state.
    edit_ranges = validate_does_not_exist(
        name, [
            DOCS_TOC_YAML_FILE,
            DOCS_ROOT_README_FILE,
            CODE_ROOT_BUILD_GN_FILE,
        ])

    # Update the _toc.yaml file to include an entry pointing to this canonical example series.
    with open(DOCS_TOC_YAML_FILE, "wt") as f:
        lines, start, end = edit_ranges[DOCS_TOC_YAML_FILE]
        lines.insert(
            end,
            """    path: /docs/development/languages/fidl/examples/%s/README.md\n"""
            % name)
        lines.insert(
            end, """  - title: "%s"\n""" % subs['series_sentence_case'])
        f.write("".join(lines))

    # Update the root README to include this series.
    with open(DOCS_ROOT_README_FILE, "wt") as f:
        lines, start, end = edit_ranges[DOCS_ROOT_README_FILE]
        lines.insert(
            end,
            """## %s\n\n<!-- TODO(fxbug.dev/%s): Add documentation (brief description) -->\n\n"""
            % (subs['series_sentence_case'], subs['bug']))
        f.write("".join(lines))

    # Update /examples/fidl/new/BUILD.gn to support this series.
    with open(CODE_ROOT_BUILD_GN_FILE, "wt") as f:
        lines, start, end = edit_ranges[CODE_ROOT_BUILD_GN_FILE]
        lines.insert(
            end - 1, """    "%s:tests",\n""" % subs['series_snake_case'])
        f.write("".join(lines))

    # Copy over each doc template file, processing substitutions on both file names and contents as
    # we go.
    dns_counts = {}
    for source, target in [(CREATE_DOCS_TEMPLATES_DIR, EXAMPLE_DOCS_BASE_DIR),
                           (CREATE_CODE_TEMPLATES_DIR, EXAMPLE_CODE_BASE_DIR)]:
        for path, count in apply_subs(name, subs, source, target).items():
            dns_counts[path] = count

    # Print the DNS counts, so that that the user may act on them.
    if len(dns_counts):
        print(
            textwrap.dedent(
                """
            Success!

            Several generated files have comments of the form "TODO(%s)" in them. Please replace
            those with the appropriate content, as specified by the comments:
        """ % DNS))
        for path, count in dns_counts.items():
            if count > 0:
                print("    * %d occurrences in %s" % (count, path))
        print()


def create(name, protocol, bug, series):
    """Create a new variant in a canonical example series.
    """
    if not series:
        return create_new(name, protocol, bug)

    # TODO(fxbug.dev/111614): Add support for extending existing series.
    raise argparse.ArgumentTypeError(
        "Adding variants to existing series not yet supported: https://fxbug.dev/111614"
    )


def pascal_case(arg):
    """Check that command-line argument strings are PascalCase.
    """
    if not is_pascal_case(arg):
        raise argparse.ArgumentTypeError("'%s' must be PascalCase" % arg)
    return arg


def snake_case(arg):
    """Check that command-line argument strings are snake_case.
    """
    if not is_snake_case(arg):
        raise argparse.ArgumentTypeError("'%s' must be snake_case" % arg)
    return arg


def unsigned_int(arg):
    """Argument parsing helper to validate unsigned integers.
    """
    as_int = int(arg)
    if as_int <= 0:
        raise argparse.ArgumentTypeError(
            "'%s' must be an unsigned integer" % as_int)
    return as_int


def main(args):
    """Scaffolding scripts for the FIDL canonical examples effort.
    """
    if args.command_used == "create":
        return create(args.name, args.protocol, args.bug, args.series)
    raise argparse.ArgumentTypeError("Unknown command '%s'" % args.command_used)


if __name__ == '__main__':
    details = {
        'create':
            """Create a new canonical example entry, with `TODO.md` placeholders files in place of
            future implementations.""",
    }
    helps = {
        'bug':
            """The bug associated with this canonical example entry.""",
        'from':
            """The name of the canonical example series that this example is an extension of. If
            this argument is omitted, the --new flag must be specified instead to indicate that we
            are creating the baseline case of a brand new canonical example series of the specified
            name.""",
        'new':
            """If this is a new canonical example series, this flag needs to be included to indicate
            this fact.""",
        'name':
            """The name of the canonical example variant being affected by this command.""",
        'protocol':
            """The @discoverable protocol that serves as the first contact point for the client and
            server in this example.""",
    }
    args = argparse.ArgumentParser(
        description="Create or modify FIDL canonical example.s")
    commands = args.add_subparsers(
        dest="command_used",
        help="Commands (specify command followed by --help for details)",
    )

    # Specify the create command, used to create a new canonical example series, or a new variant in
    # an already existing one.
    create_cmd = commands.add_parser("create", help=details["create"])
    create_cmd.add_argument(
        "name", metavar="name", type=snake_case, help=helps['name'])
    create_cmd.add_argument(
        "--protocol", type=pascal_case, required=True, help=helps['protocol'])
    create_cmd.add_argument(
        "--bug", type=unsigned_int, required=True, help=helps['bug'])
    create_cmd_new_or_extend = create_cmd.add_mutually_exclusive_group(
        required=True)
    create_cmd_new_or_extend.add_argument(
        "--from", dest="series", type=snake_case, help=helps['from'])
    create_cmd_new_or_extend.add_argument(
        "--new", action='store_true', help=helps['new'])
    create_cmd_new_or_extend.set_defaults(new=True)

    # Parse arguments.
    main(args.parse_args())
