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
CONCEPT_DOCS_BASE_DIR = FIDL_DOCS_ROOT_DIR / "concepts"
DOCS_TOC_YAML_FILE = FIDL_DOCS_ROOT_DIR / "_toc.yaml"
DOCS_ROOT_README_FILE = EXAMPLE_DOCS_BASE_DIR / "README.md"
CODE_ROOT_BUILD_GN_FILE = EXAMPLE_CODE_BASE_DIR / "BUILD.gn"

# Template source paths.
TEMPLATES_DIR = FUCHSIA_DIR / "tools/fidl/scripts/canonical_example/templates"
CREATE_CODE_NEW_TEMPLATES_DIR = TEMPLATES_DIR / "create_code_new"
CREATE_CODE_VARIANT_TEMPLATES_DIR = TEMPLATES_DIR / "create_code_variant"
CREATE_DOCS_NEW_TEMPLATES_DIR = TEMPLATES_DIR / "create_docs_new"
CREATE_DOCS_VARIANT_TEMPLATES_DIR = TEMPLATES_DIR / "create_docs_variant"
DOCUMENT_CONCEPT_TEMPLATES_DIR = TEMPLATES_DIR / "document_concept"
DOCUMENT_WIDGET_TEMPLATES_DIR = TEMPLATES_DIR / "document_widget"

# Startup-compiled regexes.
REGEX_IS_PASCAL_CASE = re.compile(r"^(?:[A-Z][a-z0-9]+)+$")
REGEX_IS_SNAKE_CASE = re.compile(r"^[a-z][a-z0-9_]+$")
REGEX_TO_TEXT_CASE = re.compile(r"_+")

# Other important defaults.
BASELINE = "baseline"
DNS = "DO" + "NOT" + "SUBMIT"
DNRC = "DO_NOT_REMOVE_COMMENT"


# Custom exceptions.
class AlreadyExistsError(Exception):
    pass


class InputError(Exception):
    pass


class StepError(Exception):
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
    if protocol and not is_pascal_case(protocol):
        raise InputError("Expected protocol '%s' to be pascal_case" % protocol)
    if bug and not (isinstance(bug, int) and bug > 0):
        raise InputError("Expected bug '%s' to be an unsigned integer" % bug)

    # LINT.IfChange
    return {
        'bug': "%s" % bug if bug else "",
        'dns': DNS,
        'protocol_pascal_case': protocol if protocol else "",
        'series_flat_case': to_flat_case(series),
        'series_sentence_case': to_sentence_case(series),
        'series_snake_case': series,
        'series_text_case': to_text_case(series),
        'variant_flat_case': to_flat_case(variant),
        'variant_snake_case': variant,
    }
    # LINT.ThenChange(/tools/fidl/scripts/canonical_example/README.md)


def build_doc_subs(series, variant):
    """Builds the substitution map, but ignores `protocol` and `bug`.
    """
    return build_subs(series, variant, None, None)


def get_dns_count(input):
    """Count `TODO(DNS)` occurrences.
    """
    return input.count("TODO(%s)" % DNS)


def apply_subs(subdir, subs, source, target):
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
            out_path = target / subdir / Template(
                str(rel_path)).substitute(subs)

            # Do substitutions on the file text as well.
            out_contents = ""
            with open(temp_path, 'r') as f:
                out_contents = Template(f.read()).substitute(subs)

            # Count DNS occurrences.
            dns_counts[str(out_path)] = get_dns_count(out_contents)

            # Do the actual file writes.
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(out_path, "wt") as f:
                f.write(out_contents)

    return dns_counts


def lines_between_dnrc_tags(lines, src_path, identifier=""):
    """Take a list of lines, and return the sub-list between the DNRC tags.

    Returns the selected lines, plus the index at which they are located in the source file.
    """
    if identifier:
        identifier = ":" + identifier
    REGEX_OPEN_DNRC = re.compile("\\sDO_NOT_REMOVE_COMMENT%s" % identifier)
    REGEX_CLOSE_DNRC = re.compile("\\s\/DO_NOT_REMOVE_COMMENT%s" % identifier)

    opened = -1
    for line_num, line in enumerate(lines):
        if opened >= 0:
            if REGEX_CLOSE_DNRC.search(line):
                return opened + 1, line_num
        elif REGEX_OPEN_DNRC.search(line):
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
    for path, identifier in files.items():
        # Find `DO_NOT_REMOVE_COMMENT` and `/DO_NOT_REMOVE_COMMENT`
        lines = []
        with open(path, 'r') as f:
            lines = f.readlines()
            start, end = lines_between_dnrc_tags(lines, path, identifier)

        # Does the needle appear anywhere in between those bounds?
        for line in lines:
            if needle in line:
                raise AlreadyExistsError(
                    "An entry for '%s' already exists in '%s'" % (needle, path))

        ranges[path] = (lines, start, end)
    return ranges


def resolve_create_templates(subs, source_to_target_map):
    """Do all of the raw copying necessary to create a canonical example variant.

    Returns a dict of paths to numbers, wih the number representing the count of outstanding DNS
    TODOs in the created files.
    """
    # Copy over each doc template file, processing substitutions on both file names and contents as
    # we go.
    dns_counts = {}
    for source, target in source_to_target_map.items():
        for path, count in apply_subs(subs['series_snake_case'], subs, source,
                                      target).items():
            dns_counts[path] = count
    return dns_counts


def resolve_document_templates(subs):
    """Do all of the raw copying necessary to document a single concept.

    Returns a dict of paths to numbers, wih the number representing the count of outstanding DNS
    TODOs in the file.
    """
    dns_counts = {}
    source_to_subdir_map = {
        DOCUMENT_CONCEPT_TEMPLATES_DIR: "concepts",
        DOCUMENT_WIDGET_TEMPLATES_DIR: "widgets",
    }
    for source, subdir in source_to_subdir_map.items():
        for path, count in apply_subs(subdir, subs, source,
                                      FIDL_DOCS_ROOT_DIR).items():
            dns_counts[path] = count
    return dns_counts


def create_variant(series, variant, protocol, bug):
    """Create a canonical example series variant.

    Returns a dict of paths to numbers, wih the number representing the count of outstanding DNS
    TODOs in the file.
    """
    # Build the substitution dictionary.
    subs = build_subs(series, variant, protocol, bug)

    # Make sure the baseline case exists. Also specifically validate that the baseline FIDL
    # definition and top-level docs README both exist, as we will need to modify them directly.
    series_root_gn_file_path = EXAMPLE_CODE_BASE_DIR / series / "BUILD.gn"
    baseline_code_dir_path = EXAMPLE_CODE_BASE_DIR / series / "baseline"
    series_docs_dir_path = EXAMPLE_DOCS_BASE_DIR / series
    baseline_fidl_file_path = baseline_code_dir_path / "fidl" / (
        "%s.test.fidl" % series)
    series_docs_readme_path = series_docs_dir_path / "README.md"
    if not os.path.exists(baseline_code_dir_path):
        raise StepError(
            "Cannot create variant '%s' because its baseline source directory %s does not exist"
            % (variant, baseline_code_dir_path))
    if not os.path.exists(series_docs_dir_path):
        raise StepError(
            "Cannot create variant '%s' because its series example docs directory %s does not exist"
            % (variant, series_docs_dir_path))
    if not os.path.exists(baseline_fidl_file_path):
        raise StepError(
            "Cannot create variant '%s' because its baseline FIDL file %s does not exist"
            % (variant, baseline_fidl_file_path))

    if not os.path.exists(series_root_gn_file_path):
        raise StepError(
            "Cannot create variant '%s' because its series BUILD.gn file %s does not exist"
            % (variant, series_root_gn_file_path))
    series_gn_lines = validate_does_not_exist(
        "{#%s}" % variant, [series_root_gn_file_path])[series_root_gn_file_path]

    if not os.path.exists(series_docs_readme_path):
        raise StepError(
            "Cannot create variant '%s' because its series README.md file %s does not exist"
            % (variant, series_docs_readme_path))
    readme_dnrc_lines = validate_does_not_exist(
        "{#%s}" % variant,
        {series_docs_readme_path: ''})[series_docs_readme_path]

    # Create the raw variant. We'll follow this up by replacing the "raw" FIDL file with a copy
    # of the one from the baseline case.
    dns_counts = resolve_create_templates(
        subs, {
            CREATE_DOCS_VARIANT_TEMPLATES_DIR: EXAMPLE_DOCS_BASE_DIR,
            CREATE_CODE_VARIANT_TEMPLATES_DIR: EXAMPLE_CODE_BASE_DIR,
        })

    # Copy the FIDL definition from the baseline case, and update the dns count so the user is aware
    # that they need to make further edits.
    variant_fidl_file_path = EXAMPLE_CODE_BASE_DIR / series / variant / "fidl" / (
        "%s.test.fidl" % series)
    with open(baseline_fidl_file_path, "r") as f:
        # Replace the ".baseline" library name suffix with this variant's name.
        lines = f.readlines()
        for i in range(len(lines)):
            if lines[i].startswith("library"):
                lines[i] = lines[i].replace(
                    BASELINE, subs['variant_flat_case'], 1)
                break

        # Add a DNS to the end of the file.
        lines.append(
            textwrap.dedent(
                """
                // TODO(%s): Modify this FIDL to reflect the changes explored by this variant.
                """ % DNS))

        # Overwrite the raw FIDL file with this transformed baseline version, and update the dns
        # count so the user is aware that they need to make further edits in this file.
        out_contents = "".join(lines)
        dns_counts[str(variant_fidl_file_path)] = get_dns_count(out_contents)
        with open(variant_fidl_file_path, "wt") as f:
            f.write(out_contents)

    # Add an entry to the docs BUILD.gn file for this series, and update the dns count so the user
    # is aware that they need to make further edits in this file.
    with open(series_root_gn_file_path, "wt") as f:
        lines, start, end = series_gn_lines
        lines.insert(end - 1, """    "%s:tests",\n""" % variant)
        f.write("".join(lines))

    # Add an entry to the root docs README.md file's example section for this series, and update the
    # dns count so the user is aware that they need to make further edits in this file.
    with open(series_docs_readme_path, "wt") as f:
        lines, start, end = readme_dnrc_lines
        lines.insert(end, """<<_%s_tutorial.md>>\n\n""" % variant)
        lines.insert(
            end,
            """### <!-- TODO(%s): Add title -->{#%s}\n\n""" % (DNS, variant))
        out_contents = "".join(lines)
        dns_counts[str(series_docs_readme_path)] = get_dns_count(out_contents)
        f.write(out_contents)

    return dns_counts


def create_new(series, protocol, bug):
    """Create a new canonical example series.

    Returns a dict of paths to numbers, wih the number representing the count of outstanding DNS
    TODOs in the file.
    """
    # Build the substitution dictionary.
    subs = build_subs(series, BASELINE, protocol, bug)

    # Do a first pass through the files we'll be editing, to ensure that they are in a good state.
    edit_ranges = validate_does_not_exist(
        series, {
            DOCS_TOC_YAML_FILE: '',
            DOCS_ROOT_README_FILE: 'examples',
            CODE_ROOT_BUILD_GN_FILE: '',
        })

    # Update the _toc.yaml file to include an entry pointing to this canonical example series.
    with open(DOCS_TOC_YAML_FILE, "wt") as f:
        lines, start, end = edit_ranges[DOCS_TOC_YAML_FILE]
        lines.insert(
            end,
            """    path: /docs/development/languages/fidl/examples/%s/README.md\n"""
            % series)
        lines.insert(
            end, """  - title: "%s"\n""" % subs['series_sentence_case'])
        f.write("".join(lines))

    # Update the root README to include this series.
    with open(DOCS_ROOT_README_FILE, "wt") as f:
        lines, start, end = edit_ranges[DOCS_ROOT_README_FILE]
        lines.insert(
            end,
            """## %s\n\n<!-- TODO(fxbug.dev/%s): DOCUMENT[%s/%s] (brief description) -->\n\n"""
            % (
                subs['series_sentence_case'], subs['bug'],
                subs['series_snake_case'], subs['variant_snake_case']))
        f.write("".join(lines))

    # Update /examples/fidl/new/BUILD.gn to support this series.
    with open(CODE_ROOT_BUILD_GN_FILE, "wt") as f:
        lines, start, end = edit_ranges[CODE_ROOT_BUILD_GN_FILE]
        lines.insert(
            end - 1, """    "%s:tests",\n""" % subs['series_snake_case'])
        f.write("".join(lines))

    # Copy over each doc template file, processing substitutions on both file names and contents as
    # we go.
    dns_counts = resolve_create_templates(
        subs, {
            CREATE_DOCS_NEW_TEMPLATES_DIR: EXAMPLE_DOCS_BASE_DIR,
            CREATE_DOCS_VARIANT_TEMPLATES_DIR: EXAMPLE_DOCS_BASE_DIR,
            CREATE_CODE_NEW_TEMPLATES_DIR: EXAMPLE_CODE_BASE_DIR,
            CREATE_CODE_VARIANT_TEMPLATES_DIR: EXAMPLE_CODE_BASE_DIR,
        })

    return dns_counts


def create(name, protocol, bug, series):
    """Create a new variant in a canonical example series.

    Can create an entirely new series or simply extend an existing one.
    """
    dns_counts = {}
    if series:
        dns_counts = create_variant(series, name, protocol, bug)
    else:
        dns_counts = create_new(name, protocol, bug)

    report_success(dns_counts)


def document(name, concepts):
    """Generate documentation instructions for an already-created canonical example variant.
    """
    # Validate that the variant already exists. The name argument can refer to either a series'
    # baseline case or a named variant, so try both options.
    series = ""
    variant = ""
    for outer in os.listdir(EXAMPLE_DOCS_BASE_DIR):
        maybe_dir = os.path.join(str(EXAMPLE_DOCS_BASE_DIR), outer)
        if os.path.isdir(maybe_dir):
            if outer == name:
                series = name
                variant = BASELINE
                break
            else:
                for inner in os.listdir(maybe_dir):
                    if inner.startswith("_%s" % name):
                        series = outer
                        variant = name
                        break
    if not (series and variant):
        raise StepError(
            "Cannot find already created variant '%s' to document" % name)

    # Validate that the root docs README.md file exists, and that its concepts section is well
    # marked, since we will need to modify it.
    readme_concepts_lines = validate_does_not_exist(
        "### %s" % variant,
        {DOCS_ROOT_README_FILE: 'concepts'})[DOCS_ROOT_README_FILE]

    # Build the substitution dictionary. We don't need to do protocol or bug number substitution, so
    # ignore those arguments.
    subs = build_doc_subs(series, variant)

    # Create the files for each concept.
    dns_counts = {}
    for concept in concepts:
        subs['concept_snake_case'] = concept
        subs['concept_sentence_case'] = to_sentence_case(concept)

        # Add an entry to the root docs README.md file's example section for this concept, and update
        # the dns count so the user is aware that they need to make further edits in this file.
        with open(DOCS_ROOT_README_FILE, "wt") as f:
            lines, start, end = readme_concepts_lines
            lines.insert(end, """<<../concepts/_%s.md>>\n\n""" % concept)
            lines.insert(end, """### %s\n\n""" % subs['concept_sentence_case'])
            out_contents = "".join(lines)
            f.write(out_contents)

        # Now generate the actual file from a template.
        for path, count in resolve_document_templates(subs).items():
            dns_counts[path] = count

    # Replace documentation TODOs generated in the create step with DNS ones.
    REGEX_DOCUMENT_TODO = re.compile(
        "TODO\\(fxbug.dev\\/\\d+\\): DOCUMENT\\[%s\\/%s]" % (series, variant))
    possible_add_doc_files = [
        DOCS_ROOT_README_FILE,
        EXAMPLE_DOCS_BASE_DIR / series / "README.md",
        EXAMPLE_DOCS_BASE_DIR / series / ("_%s_tutorial.md" % variant),
    ]
    for path in possible_add_doc_files:
        with open(path, "r") as f:
            path_str = str(path)
            replaced, count = re.subn(
                REGEX_DOCUMENT_TODO, "TODO(%s):" % DNS, f.read())
            if dns_counts.get(path_str):
                dns_counts[path_str] = dns_counts[path_str] + count
            else:
                dns_counts[path_str] = count
            with open(path, "wt") as f:
                f.write(replaced)

    report_success(dns_counts)


def report_success(dns_counts):
    """Print some useful output to the user describing what they need to do next.
    """
    if len(dns_counts):
        print(
            textwrap.dedent(
                """
            Success!

            Several generated files contain comments of the form "TODO(%s)".
            Please replace them with the appropriate content, as specified by in their descriptions:
        """ % DNS))
        for path, count in dns_counts.items():
            if count > 0:
                print("    * %d occurrences in %s" % (count, path))
        print()


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


def snake_case_list(arg):
    """Check that command-line argument lists are all snake_case strings.
    """
    as_list = []
    for item in arg.split(","):
        item = item.strip()
        if not is_snake_case(item):
            raise argparse.ArgumentTypeError(
                "'%s' must be a list of snake_case strings" % arg)
        as_list.append(item)
    return as_list


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
    if args.command_used == "document":
        return document(args.name, args.concepts[0])
    raise argparse.ArgumentTypeError("Unknown command '%s'" % args.command_used)


if __name__ == '__main__':
    details = {
        'create':
            """Create a new canonical example variant, with `TODO.md` placeholders files in place of
            future implementations.""",
        'document':
            """Document a canonical example variant, which enables writing all of the tutorial text
            that accompanies the variant.""",
    }
    helps = {
        'bug':
            """The bug associated with this canonical example entry.""",
        'concepts':
            """The concepts associated with this variant - each will receive its own widget.""",
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

    # Specify the document command.
    document_cmd = commands.add_parser("document", help=details["document"])
    document_cmd.add_argument(
        "name", metavar="name", type=snake_case, help=helps['name'])
    document_cmd.add_argument(
        "--concepts", nargs='+', type=snake_case_list, help=helps['concepts'])

    # Parse arguments.
    main(args.parse_args())
