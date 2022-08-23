# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Test to prevent changes to SDK's JSON metadata schemas.
Ensures that developers will not unintentionally break down stream
customers by changing the JSON schemas without knowing the effect
the change will have.
"""

import argparse
import textwrap
import json
import os
import sys

width = 4


def get_pretty_str(changes, str_list=None, indent=0):
    if not str_list:
        str_list = []
    for key, value in changes.items():
        str_list.append(textwrap.indent(f"{str(key)}:", indent * ' '))
        if isinstance(value, dict):
            get_pretty_str(value, str_list, indent + width)
        elif isinstance(value, list):
            for item in value:
                if isinstance(item, dict):
                    get_pretty_str(item, str_list, indent + width)
        else:
            str_list.append(
                textwrap.indent(f"{str(value)}", (indent + width) * ' '))
    if indent == 0:
        return '\n'.join(str_list)


class GoldenMismatchError(Exception):
    """Exception raised when golden and current are not identical."""

    def __init__(self, mismatch, breaks, non_breaks):
        self.breaks = breaks
        self.non_breaks = non_breaks

        cmd_str = ""
        for path in mismatch:
            cmd_str += "\n" + update_cmd(path[0], path[1])
        self.cmd_str = cmd_str

    def __str__(self):
        # Notice of any changes detected, including those not yet classified
        # as breaking or non-breaking.
        ret_str = (
            f"Detected changes to JSON Schemas.\n"
            f"All breaking changes MUST result in a change to the “id” field.\n"
            f"Please do not change the schemas without consulting"
            f" with sdk-dev@fuchsia.dev.\n"
            f"To prevent breaking SDK integrators, "
            f"the contents of current schemas should match goldens.\n")
        if self.breaks:
            formatted_breaks = get_pretty_str(self.breaks)
            ret_str += textwrap.indent(
                "Breaking Changes:", width * ' ') + "\n" + textwrap.indent(
                    f"{formatted_breaks}", 2 * width * ' ') + "\n"
        if self.non_breaks:
            formatted_non_breaks = get_pretty_str(self.non_breaks)
            ret_str += textwrap.indent(
                "Non-Breaking Changes:", width * ' ') + "\n" + textwrap.indent(
                    f"{formatted_non_breaks}", 2 * width * ' ') + "\n"
        ret_str += f"If you have approval to make this change, run: {self.cmd_str}\n"
        return ret_str


# TODO(fxb/107079): Update JSON schemas check to be a compatibility test.
# class BreakingChangesError(Exception):
#     Exception raised when breaking changes are detected.

#     def __init__(self, changes):
#         self.changes = changes
#         cmd_str = ""
#         for key in changes.keys():
#             cmd_str += "\n" + update_cmd(key[1], key[0])
#         self.cmd_str = cmd_str

#     def __str__(self):
#         return (
#             f"Detected the following breaking changes:\n"
#             f"{self.changes}\n"
#             f"Please do not make this change without consulting"
#             f" with sdk-dev@fuchsia.dev.\n"
#             f"To prevent potential breaking SDK integrators, "
#             f"the contents of current schemas should match goldens.\n"
#             f"If you have approval to make this change, run:{self.cmd_str}\n")

# class NotifyNonBreakingChanges(Exception):
#     Exception raised when non-breaking changes are detected.

#     def __init__(self, changes):
#         self.changes = changes
#         cmd_str = ""
#         for key in changes.keys():
#             cmd_str += "\n" + update_cmd(key[1], key[0])
#         self.cmd_str = cmd_str

#     def __str__(self):
#         return (
#             f"Detected the following non-breaking changes:\n{self.changes}\n"
#             f"If you want to continue with this change, run:{self.cmd_str}\n")


class InvalidJsonError(Exception):
    """Exception raised when invalid JSON in a golden file is detected."""

    def __init__(self, invalid_schema):
        self.invalid_schema = invalid_schema

    def __str__(self):
        return (
            f"Detected invalid JSON Schema {self.invalid_schema}.\n"
            f"Consult with sdk-dev@fuchsia.dev to update this golden file.\n")


class MissingInputError(Exception):
    """Exception raised when a missing golden,
    current, or stamp file is detected."""

    def __init__(self, missing, schema=True):
        self.missing = missing
        self.schema = schema

    def __str__(self):
        if self.schema:
            return (
                f"Detected missing JSON Schema {self.missing}.\n"
                f"Please consult with sdk-dev@fuchsia.dev if you are"
                f" planning to remove a schema.\n"
                f"If you have approval to make this change, remove the "
                f"schema and corresponding golden file from the schema lists.\n"
            )
        return (
            f"The verification file path appears to be missing:\n{self.missing}\n"
        )


class SchemaListMismatchError(Exception):
    """Exception raised when the golden list and current
    list contain a different number of schemas or when
    there is a mismatch of the schema filenames."""

    def __init__(self, goldens, currents, err_type):
        self.goldens = goldens
        self.currents = currents
        self.err_type = err_type

    def __str__(self):
        if self.err_type == 0:
            return (
                f"Detected that the golden list contains "
                f"a different number of schemas than the current list.\n"
                f"Golden:\n{self.goldens}\nCurrent:\n{self.currents}\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
        else:
            return (
                f"Detected that filenames in the golden list, {self.goldens},"
                f" do not correspond to those in the current list, "
                f"{self.currents}.\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--golden',
        nargs='+',
        help="Files in the golden directory",
        required=True)
    parser.add_argument(
        '--current',
        nargs='+',
        help="Files in the local directory",
        required=True)
    parser.add_argument(
        '--stamp', help="Verification output file path", required=True)
    args = parser.parse_args()

    err = None
    try:
        fail_on_breaking_changes(args.current, args.golden)
    except (SchemaListMismatchError, MissingInputError, InvalidJsonError,
            GoldenMismatchError) as e:
        err = e
    try:
        with open(args.stamp, 'w') as stamp_file:
            stamp_file.write('Verified!\n')
    except FileNotFoundError:
        err = str(MissingInputError(missing=args.stamp, schema=False))

    if not err:
        return 0
    else:
        print("ERROR: ", err)

    # Force developers to acknowledge a stale golden.
    return 1


def fail_on_breaking_changes(current_list, golden_list):
    """Fails if current and golden aren't identical."""

    if not len(golden_list) == len(current_list):
        raise SchemaListMismatchError(
            currents=current_list, goldens=golden_list, err_type=0)

    golden_list.sort()
    current_list.sort()
    total_non_breaking_changes = dict()
    total_breaking_changes = dict()
    mismatches = []

    for schema in zip(golden_list, current_list):
        if not ((os.path.basename(schema[1]) + ".golden") == os.path.basename(
                schema[0])):
            raise SchemaListMismatchError(
                currents=current_list, goldens=golden_list, err_type=1)

        # In order to not be reliant on GN side-effects, the below try-except
        # statements cover the case of a missing / invalid schema.
        # Generally, before this script runs, a build error will occur in
        # these cases.
        try:
            schema_file = open(schema[0], "r")
        except FileNotFoundError:
            raise MissingInputError(missing=schema[0],)

        try:
            schema_data = json.load(schema_file)
        except json.decoder.JSONDecodeError:
            raise InvalidJsonError(invalid_schema=schema[0],)
        schema_file.close()

        try:
            curr_file = open(schema[1], "r")
        except FileNotFoundError:
            raise MissingInputError(missing=schema[1],)

        try:
            curr_data = json.load(curr_file)
        except json.decoder.JSONDecodeError:
            mismatches += (schema[1], schema[0])
        curr_file.close()

        non_breaking_changes = []
        breaking_changes = []
        compare_schema_structure(
            curr_data, schema_data, non_breaking_changes, breaking_changes)

        if non_breaking_changes:
            total_non_breaking_changes[schema] = non_breaking_changes
        if breaking_changes:
            total_breaking_changes[schema] = breaking_changes
        if not schema_data == curr_data:
            mismatches.append((schema[1], schema[0]))

    if mismatches:
        raise GoldenMismatchError(
            mismatch=mismatches,
            breaks=total_breaking_changes,
            non_breaks=total_non_breaking_changes)

    return None


# Compare the input schemas' keys. Return breaking and non-breaking changes.
def compare_schema_structure(
        curr_data,
        gold_data,
        non_breaking=[],
        breaking_changes=[],
        level="root"):
    if isinstance(curr_data, dict) and isinstance(gold_data, dict):
        curr_keys = set(curr_data.keys())
        gold_keys = set(gold_data.keys())
        if curr_data.keys() != gold_data.keys():
            if gold_keys.difference(curr_keys):
                breaking_changes.append(
                    {
                        f"Missing keys of {level}":
                            set(gold_keys.difference(curr_keys)),
                    })
            if curr_keys.difference(gold_keys):
                non_breaking.append(
                    {
                        f"New keys of {level}":
                            set(curr_keys.difference(gold_keys)),
                    })
        if 'required' in curr_keys and 'required' in gold_keys:
            if isinstance(curr_data["required"], list):
                gold_data["required"].sort()
                curr_data["required"].sort()
                if gold_data["required"] != curr_data["required"]:
                    breaking_changes.append(
                        {
                            f"'required' parameters changed on {level}":
                                {
                                    "golden": set(gold_data["required"]),
                                    "current": set(curr_data["required"]),
                                },
                        })
            else:
                breaking_changes.append(
                    {
                        f"'required' parameters changed on {level}":
                            {
                                "golden": set(gold_data["required"]),
                                "current": curr_data["required"],
                            },
                    })

        for key in (gold_keys.intersection(curr_keys)):
            compare_schema_structure(
                curr_data[key], gold_data[key], non_breaking, breaking_changes,
                f"{level}.{key}")
    elif isinstance(gold_data, dict):
        breaking_changes.append({
            f"Missing keys of {level}": set(gold_data.keys()),
        })
    elif isinstance(curr_data, dict):
        non_breaking.append({
            f"New keys of {level}": set(curr_data.keys()),
        })


def update_cmd(current, golden):
    """For presentation only. Never execute the cmd output pragmatically because
    it may present a security exploit."""
    return "cp \"{}\" \"{}\"".format(
        os.path.abspath(current), os.path.abspath(golden))


if __name__ == '__main__':
    sys.exit(main())
