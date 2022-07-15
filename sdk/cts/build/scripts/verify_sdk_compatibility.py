# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Test to prevent integrators from having future breakages caused
by instability of the Fuchsia SDK package directory.
"""

import argparse
import json
import os
import sys
import tarfile
from enum import Enum


class Policy(Enum):

    # Tells the check to not run. Used for uploading intentional changes.
    update_golden = "update_golden"

    # Communicates the intent of making changes.
    ack_changes = "ack_changes"

    # Tells this script to fail if breaking changes are detected.
    no_breaking_changes = "no_breaking_changes"

    # Tells this script to fail if any changes are detected.
    no_changes = "no_changes"

    def __str__(self):
        return self.value


class GoldenLayoutMismatchError(Exception):
    """Exception raised when a stale golden file is detected."""

    def __init__(self, api_level, current, golden, length_err):
        self.api_level = api_level
        self.current = current
        self.golden = golden
        self.length_err = length_err

    def __str__(self):
        return_str = (
            f"Detected changes to API level {self.api_level} directory.\n"
            f"Please do not change the directory layout without consulting"
            f" with sdk-dev@fuchsia.dev.\n"
            f"To prevent potential breaking of SDK integrators,"
            f"\n{self.current}\nshould correspond to\n{self.golden}\n")
        if self.length_err:
            return_str += (
                f"Specifically, the number of items described in both"
                f" layouts should be equal.\n")
        return_str += (
            f"If you have approval to make this change, please update the"
            f" golden file and rerun with `--policy update_golden`.\n")
        return return_str


class InvalidJsonError(Exception):
    """Exception raised when invalid JSON in a golden file is detected."""

    def __init__(self, invalid):
        self.invalid = invalid

    def __str__(self):
        return (
            f"Detected invalid JSON within the golden file:\n{self.invalid}.\n"
            f"Consult with sdk-dev@fuchsia.dev to update this golden file.\n")


class MissingInputError(Exception):
    """Exception raised when a missing golden file
    or current archive tarball is detected."""

    def __init__(self, missing, is_tar):
        self.missing = missing
        self.is_tar = is_tar

    def __str__(self):
        input = "golden file"
        if self.is_tar:
            input = "current archive tarball"
        return_str = f"The {input} appears to be missing:\n{self.missing}.\n"
        if not self.is_tar:
            return_str += (
                f"Please consult with sdk-dev@fuchsia.dev to"
                f" locate and resolve this missing golden file.\n")
        return return_str


class SdkCompatibilityError(Exception):
    """Exception raised when breaking API changes are detected"""

    def __init__(self, api_level, current, golden, length_err):
        self.api_level = api_level
        self.current = current
        self.golden = golden
        self.length_err = length_err

    def __str__(self):
        return_str = (
            f"These changes are incompatible with API level {self.api_level}\n"
            f"If possible, please make a soft transition instead.\n"
            f"To prevent potential breaking of SDK integrators,")
        if not self.length_err:
            return_str += (
                f"\n{self.current}\n should contain a path corresponding to {self.golden}.\n"
                f"If you have approval to make this change, please update the golden path to"
                f" correspond to the current archive tarball and rerun"
                f" with `--policy update_golden`.\n")
        else:
            return_str += (
                f" the only changes made should be additions.\nThe current"
                f" archive tarball should not have fewer items than described"
                f" within the golden file.\n"
                f"If you have approval to make this change, please update the golden file to"
                f" correspond to the current archive tarball and"
                f" rerun with `--policy update_golden`.\n")
        return return_str


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--policy',
        help='How to handle failures',
        type=Policy,
        default=Policy.no_breaking_changes,
        choices=list(Policy))
    parser.add_argument(
        '--api-level', help='The API level being tested', required=True)
    parser.add_argument(
        '--golden', help='Path to the golden file', required=True)
    parser.add_argument(
        '--current',
        help='Path to the local SDK archive tarball',
        required=True)
    parser.add_argument(
        '--stamp', help='Verification output file path', required=True)
    parser.add_argument(
        '--warn_on_changes',
        help='Treat compatibility violations as warnings',
        action='store_true')
    args = parser.parse_args()

    err = None
    if args.policy == Policy.update_golden:
        try:
            fail_on_changes(args.current, args.golden, args.api_level)
        except (GoldenLayoutMismatchError, MissingInputError,
                InvalidJsonError) as err:
            print("WARNING: ", err)
    elif args.policy == Policy.no_breaking_changes:
        try:
            fail_on_breaking_changes(args.current, args.golden, args.api_level)
        except (SdkCompatibilityError, MissingInputError,
                InvalidJsonError) as e:
            err = e
    elif (args.policy == Policy.no_changes) or (args.policy
                                                == Policy.ack_changes):
        try:
            fail_on_changes(args.current, args.golden, args.api_level)
        except (GoldenLayoutMismatchError, MissingInputError,
                InvalidJsonError) as e:
            err = e
    else:
        raise ValueError("unknown policy: {}".format(args.policy))

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Verified!\n')

    if not err or (args.policy == Policy.update_golden):
        return 0

    if args.warn_on_changes:
        print("WARNING: ", err)
        return 0

    print("ERROR: ", err)
    return 1


def fail_on_breaking_changes(current_a, golden_a, api_level_a):
    """Fails if current is not compatible with golden or if
    current and golden aren't identical.
    """
    gold_paths, curr_layout = file_preparation(
        current_a, golden_a, api_level_a, True)

    if len(gold_paths) > len(curr_layout):
        raise SdkCompatibilityError(
            api_level=api_level_a,
            current=current_a,
            golden=golden_a,
            length_err=True,
        )

    sorted_golden = sorted(gold_paths, key=lambda d: d['name'])
    sorted_curr = sorted(curr_layout, key=lambda m: m.name)

    for path in sorted_golden:
        # See if the golden path is in current directory layout.
        gold_path = path.get("name")
        if gold_path is None:
            raise InvalidJsonError(invalid=path)
        if len(sorted_curr) > 0:
            match = sorted_curr.pop(0)
        else:
            raise SdkCompatibilityError(
                api_level=api_level_a,
                current=current_a,
                golden=path,
                length_err=False,
            )
        while not ((match.name == gold_path) and
                   ((match.isfile() and path.get("type") == "file") or
                    (match.isdir() and path.get("type") == "directory"))):
            if len(sorted_curr) > 0:
                match = sorted_curr.pop(0)
            else:
                raise SdkCompatibilityError(
                    api_level=api_level_a,
                    current=current_a,
                    golden=path,
                    length_err=False,
                )

    # Make the developer acknowledge a stale golden file.
    return fail_on_changes(current_a, golden_a, api_level_a, False)


def fail_on_changes(current_a, golden_a, api_level_a, keep_check=True):
    """Fails if current and golden aren't identical."""
    gold_paths, curr_layout = file_preparation(
        current_a, golden_a, api_level_a, False)

    if len(gold_paths) != len(curr_layout):
        raise GoldenLayoutMismatchError(
            api_level=api_level_a,
            current=current_a,
            golden=golden_a,
            length_err=True,
        )

    if keep_check:
        sorted_golden = sorted(gold_paths, key=lambda d: d['name'])
        sorted_curr = sorted(curr_layout, key=lambda m: m.name)

        layout = zip(sorted_golden, sorted_curr)
        for path in layout:
            gold_path = path[0].get("name")
            if gold_path is None:
                raise InvalidJsonError(invalid=path[0])
            if not gold_path == path[1].name:
                raise GoldenLayoutMismatchError(
                    api_level=api_level_a,
                    current=path[1].name,
                    golden=path[0],
                    length_err=False,
                )

            if not ((path[1].isfile() and path[0].get("type") == "file") or
                    (path[1].isdir() and path[0].get("type") == "directory")):
                raise GoldenLayoutMismatchError(
                    api_level=api_level_a,
                    current=path[1].name,
                    golden=path[0],
                    length_err=False,
                )

    return None


def file_preparation(current_a, golden_a, api_level_a, changes_ok):
    try:
        golden_file = open(golden_a, "r")
    except FileNotFoundError:
        raise MissingInputError(missing=golden_a, is_tar=False)

    try:
        golden_layout = json.load(golden_file)
    except json.decoder.JSONDecodeError:
        raise InvalidJsonError(invalid=golden_file)
    golden_file.close()

    try:
        curr_file = tarfile.open(current_a, "r:gz")
    except tarfile.ReadError:
        raise MissingInputError(missing=current_a, is_tar=True)

    curr_layout = curr_file.getmembers()
    curr_file.close()

    gold_paths = golden_layout.get("paths")
    return gold_paths, curr_layout


if __name__ == '__main__':
    sys.exit(main())
