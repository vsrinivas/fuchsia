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
import json
import os
import sys
from enum import Enum


# TODO(fxbug.dev/66852): Add more policy options.
class Policy(Enum):
    # no_changes tells this script to fail if any changes are detected.
    # It will tell the user how to update the golden to acknowledge the
    # changes.
    no_changes = 'no_changes'


class GoldenMismatchError(Exception):
    """Exception raised when a stale golden file is detected."""

    def __init__(self, current, golden):
        self.current = current
        self.golden = golden

    def __str__(self):
        cmd = update_cmd(self.current, self.golden)
        return (
            f"Detected changes to JSON Schema {self.current}.\n"
            f"Please do not change the schemas without consulting"
            f" with sdk-dev@fuchsia.dev.\n"
            f"To prevent potential breaking SDK integrators, "
            f"the contents of {self.current} should match {self.golden}.\n"
            f"If you have approval to make this change, run: {cmd}\n")


class InvalidJsonError(Exception):
    """Exception raised when invalid JSON in a golden file is detected."""

    def __init__(self, invalid_schema):
        self.invalid_schema = invalid_schema

    def __str__(self):
        return (
            f"Detected invalid JSON Schema {self.invalid_schema}.\n"
            f"Consult with sdk-dev@fuchsia.dev to update this golden file.\n")


class MissingSchemaError(Exception):
    """Exception raised when a missing golden or current file is detected."""

    def __init__(self, missing_schema):
        self.missing_schema = missing_schema

    def __str__(self):
        return (
            f"Detected missing JSON Schema {self.missing_schema}.\n"
            f"Please consult with sdk-dev@fuchsia.dev if you are"
            f" planning to remove a schema.\n"
            f"If you have approval to make this change, remove the "
            f"schema and corresponding golden file from the schema lists.\n")


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
                f" do not match the filenames in the current list, "
                f"{self.currents}.\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--policy',
        help="How to handle failures",
        type=Policy,
        default=Policy.no_changes,
        choices=list(Policy))
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

    if args.policy == Policy.no_changes:
        err = fail_on_changes(args.current, args.golden)
        if err:
            print("ERROR: ", err)
            return 1
    else:
        raise ValueError("unknown policy: {}".format(args.policy))

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Verified!\n')
    return 0


def fail_on_changes(current_list, golden_list):
    """Fails if current and golden aren't identical."""

    if not (len(golden_list) == len(current_list)):
        return SchemaListMismatchError(
            currents=current_list, goldens=golden_list, err_type=0)

    golden_list.sort()
    current_list.sort()
    schemas = zip(golden_list, current_list)
    for schema in schemas:

        if not ((os.path.basename(schema[1]) + ".golden") == os.path.basename(
                schema[0])):
            return SchemaListMismatchError(
                currents=current_list, goldens=golden_list, err_type=1)

        # In order to not be reliant on GN side-effects, the below try-except
        # statements cover the case of a missing / invalid schema.
        # Generally, before this script runs, a build error will occur in
        # these cases.
        try:
            schema_file = open(schema[0], "r")
        except FileNotFoundError:
            return MissingSchemaError(missing_schema=schema[0],)

        try:
            schema_data = json.load(schema_file)
        except json.decoder.JSONDecodeError:
            return InvalidJsonError(invalid_schema=schema[0],)
        schema_file.close()

        try:
            curr_file = open(schema[1], "r")
        except FileNotFoundError:
            return MissingSchemaError(missing_schema=schema[1],)

        try:
            curr_data = json.load(curr_file)
        except json.decoder.JSONDecodeError:
            return GoldenMismatchError(
                current=schema[1],
                golden=schema[0],
            )
        curr_file.close()

        if not schema_data == curr_data:
            return GoldenMismatchError(
                current=schema[1],
                golden=schema[0],
            )
    return None


def update_cmd(current, golden):
    """For presentation only. Never execute the cmd output pragmatically because
    it may present a security exploit."""
    return "cp \"{}\" \"{}\"".format(
        os.path.abspath(current), os.path.abspath(golden))


if __name__ == '__main__':
    sys.exit(main())
