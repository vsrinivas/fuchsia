# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Test to prevent integrators from having future breakages caused
by instability of the Fuchsia SDK package directory.
"""

import argparse
import os
import sys
import tarfile
import re
from pathlib import Path


class GoldenFileGenerationError(Exception):
    """Exception raised when the SDK Tarball input
    contains a file which is not a regular file or directory."""

    def __init__(self, tarfile, invalid):
        self.tarfile = tarfile
        self.invalid = invalid

    def __str__(self):
        return (
            f"Detected invalid file within the SDK archive "
            f"tarfile:\n{self.invalid}.\n"
            f"Golden file will not be generated if "
            f"the tarfile contains this file type. Please update\n"
            f"{self.tarfile}\naccordingly.\n")


class MissingInputError(Exception):
    """Exception raised when a missing golden file
    or current archive tarball is detected."""

    def __init__(self, missing, is_tar=False):
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


class NotifyOnAdditions(Exception):
    """Exception raised to notify developers of non-breaking changes.
    
    Args:
        additions: Paths found in the archive but not in the golden file.
        path: Path to create a temporary golden file for the archive.
        updated_list: List of sorted paths from the SDK archive. 
        golden: Path to the golden file.
    """

    def __init__(self, additions, path, updated_list, golden):
        self.additions = additions
        self.path = path
        self.updated_list = updated_list
        self.golden = golden

    def __str__(self):
        cmd = update_golden(self.updated_list, self.path, self.golden)
        return (
            f"Detected additions to the SDK directory layout.\n"
            f"The current archive tarball contains these additional "
            f"paths not found in the golden file.\n{self.additions}\n"
            f"If this is an intentional change, run: {cmd}\n")


class SdkCompatibilityError(Exception):
    """Exception raised when breaking changes are detected.

    Args:
        idk: SDK archive tarball dependency target name.
        missing_goldens: Paths expected to be in the archive, but not found.
        path: Path to create a temporary golden file for the archive.
        updated_list: List of sorted paths from the SDK archive. 
        golden: Path to the golden file.
    """

    def __init__(self, idk, missing_goldens, path, updated_list, golden):
        self.idk = idk
        self.missing_goldens = missing_goldens
        self.path = path
        self.updated_list = updated_list
        self.golden = golden

    def __str__(self):
        cmd = update_golden(self.updated_list, self.path, self.golden)
        return (
            f"Detected breaking changes to the {self.idk} SDK's directory layout.\n"
            f"If possible, please make a soft transition"
            f" to prevent breaking SDK users.\nThe following paths are missing"
            f" from the SDK:\n{self.missing_goldens}\n"
            f"If you have approval to make this change, run: {cmd}\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--golden', help='Path to the golden file')
    parser.add_argument(
        '--current', help='Path to the SDK archive tarball', required=True)
    parser.add_argument(
        '--stamp', help='Verification output file path', required=True)
    parser.add_argument(
        '--generate_golden',
        help='Generate SDK directory layout golden file.',
        action='store_true')
    parser.add_argument(
        '--gen_golden_path',
        help='Path to generate SDK directory layout golden file.')
    parser.add_argument(
        '--update_golden',
        help='Path to generate an updated golden file when additions are made.')
    args = parser.parse_args()

    err = None
    notify = None
    if args.generate_golden:
        try:
            golden_sdk_set = generate_sdk_layout_golden_file(args.current)
        except (MissingInputError, GoldenFileGenerationError) as e:
            err = e
        golden_sdk_layout = sorted(golden_sdk_set)
        try:
            with open(args.gen_golden_path, 'w') as golden_sdk_layout_file:
                layout = "{}\n".format("\n".join(golden_sdk_layout or []))
                golden_sdk_layout_file.write(layout)
        except FileNotFoundError:
            err = str(MissingInputError(missing=args.gen_golden_path))
    else:
        try:
            fail_on_breaking_changes(
                args.current, args.golden, args.update_golden)
        except (SdkCompatibilityError, MissingInputError) as e:
            err = e
        except NotifyOnAdditions as n:
            notify = n

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Verified!\n')

    if not err:
        if notify:
            print("NOTICE: ", notify)
        return 0

    print("ERROR: ", err)
    return 1


def fail_on_breaking_changes(current_archive, golden_file, update_golden_path):
    """Fails if current is not compatible with golden."""

    gold_set = set()
    try:
        with open(golden_file, 'r') as gold_lines:
            for line in gold_lines:
                gold_set.add(line.strip())
    except FileNotFoundError:
        raise MissingInputError(missing=golden_file, is_tar=False)

    curr_set = generate_sdk_layout_golden_file(current_archive)

    set_diff = gold_set.difference(curr_set)

    # See if any golden paths are not in the current archive.
    if len(set_diff) != 0:
        raise SdkCompatibilityError(
            idk=Path(current_archive).with_suffix('').stem,
            missing_goldens=set_diff,
            path=update_golden_path,
            updated_list=sorted(curr_set),
            golden=golden_file)

    # Notify developer of any additions to the SDK directory layout.
    if len(gold_set) != len(curr_set):
        raise NotifyOnAdditions(
            additions=curr_set.difference(gold_set),
            path=update_golden_path,
            updated_list=sorted(curr_set),
            golden=golden_file)
    return None


def generate_sdk_layout_golden_file(current_archive):
    try:
        with tarfile.open(current_archive, "r:gz") as sdk_file:
            layout = sdk_file.getmembers()
    except (tarfile.ReadError, FileNotFoundError):
        raise MissingInputError(missing=current_archive, is_tar=True)

    # Ignore golden file checks for paths ending in .debug as they change frequently.
    path_regex = "\.build-id" + re.escape(os.sep) + "\w{2}" + re.escape(
        os.sep) + "\w{14}\.debug"
    ignored_path = re.compile(path_regex, re.IGNORECASE)

    golden_set = set()
    for path in layout:
        if not ignored_path.match(path.name):
            gold_dir = path.name
            if path.isdir() and not path.name.endswith(os.sep):
                # Ensure directory type paths end in a "/".
                gold_dir += os.sep
            elif not (path.isfile() or path.isdir()):
                raise GoldenFileGenerationError(
                    tarfile=current_archive, invalid=path)
            golden_set.add(gold_dir)
    return golden_set


def update_golden(updated_file_list, updated_path, golden):
    """For presentation only. Never execute the cmd output programmatically because
    it may present a security exploit."""
    with open(updated_path, 'w') as updated_golden_file:
        for path in updated_file_list or []:
            updated_golden_file.write(path)
            updated_golden_file.write("\n")
    return "cp \"{}\" \"{}\"".format(
        os.path.abspath(updated_path), os.path.abspath(golden))


if __name__ == '__main__':
    sys.exit(main())
