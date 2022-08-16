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
            f"tarfile:\n{self.invalid}\n"
            f"Golden file will not be generated if "
            f"the tarfile contains this file type. Please update\n"
            f"{self.tarfile}\naccordingly.\n")


class MissingInputError(Exception):
    """Exception raised when a missing golden file
    or current archive tarball is detected."""

    def __init__(self, missing, is_tar=False, stamp=False):
        self.missing = missing
        self.is_tar = is_tar
        self.stamp = stamp

    def __str__(self):
        input = "golden file"
        if self.is_tar:
            input = "current archive tarball"
        elif self.stamp:
            input = "verification file path"
        return_str = f"The {input} appears to be missing:\n{self.missing}\n"
        if not self.is_tar:
            return_str += (
                f"Please consult with sdk-dev@fuchsia.dev to"
                f" locate and resolve this missing golden file.\n")
        return return_str


class NotifyOnAdditions(Exception):
    """Exception raised to notify developers of non-breaking changes.

    Args:
        idk: SDK archive tarball dependency target name.
        additions: Paths found in the archive but not in the golden file.
        path: Path to create a temporary golden file for the archive.
        golden: Path to the golden file.
    """

    def __init__(self, idk, additions, path, golden):
        self.idk = idk
        self.additions = additions
        self.path = path
        self.golden = golden

    def __str__(self):
        cmd = update_golden(self.path, self.golden)
        return (
            f"Detected additions to the {self.idk} SDK's directory layout.\n"
            f"The current archive tarball contains these additional "
            f"paths not found in the golden file.\n{self.additions}\n"
            f"If this is an intentional change, run: {cmd}\n")


class SdkCompatibilityError(Exception):
    """Exception raised when breaking changes are detected.

    Args:
        idk: SDK archive tarball dependency target name.
        missing_goldens: Paths expected to be in the archive, but not found.
        path: Path to create a temporary golden file for the archive.
        golden: Path to the golden file.
    """

    def __init__(self, idk, missing_goldens, path, golden):
        self.idk = idk
        self.missing_goldens = missing_goldens
        self.path = path
        self.golden = golden

    def __str__(self):
        cmd = update_golden(self.path, self.golden)
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
    parser.add_argument('--stamp', help='Verification output file path')
    parser.add_argument(
        '--generate_golden',
        help='Generate SDK directory layout golden file.',
        action='store_true')
    parser.add_argument(
        '--gen_golden_path',
        help='Path to generate SDK directory layout golden file.')
    parser.add_argument(
        '--update_golden',
        help='Path to the updated golden file for when changes are made.')
    parser.add_argument(
        '--warn_only', help='Treat failures as warnings', action='store_true')
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
        try:
            with open(args.stamp, 'w') as stamp_file:
                stamp_file.write('Verified!\n')
        except FileNotFoundError:
            err = str(MissingInputError(missing=args.stamp, stamp=True))

    if not (err or notify):
        return 0
    elif notify:
        print("NOTICE: ", notify)
    else:
        print("ERROR: ", err)
    return 0 if args.warn_only else 1


def fail_on_breaking_changes(current_archive, golden_file, update_golden):
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
            path=update_golden,
            golden=golden_file)

    # Notify developer of any additions to the SDK directory layout.
    if len(gold_set) != len(curr_set):
        raise NotifyOnAdditions(
            idk=Path(current_archive).with_suffix('').stem,
            additions=curr_set.difference(gold_set),
            path=update_golden,
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

    # If directories in the list below exist, they're added to the set of golden paths.
    # Files in such directories will not be compatibility tested.
    dirs_exist = [
        os.path.join('tools', 'x64', 'aemu_internal', ''),
        os.path.join('tools', 'x64', 'qemu_internal', ''),
    ]

    golden_set = set()
    for path in layout:
        if ignored_path.match(path.name):
            continue
        # Set to True if the file in question is not going to be compatibility tested.
        ignoring_file = False

        # Add the directory into the golden set if it exists.
        for dir in dirs_exist:
            if path.name.startswith(dir):
                golden_set.add(dir)
                ignoring_file = True
                break
        if ignoring_file:
            continue

        gold_dir = path.name
        if path.isdir() and not path.name.endswith(os.sep):
            # Ensure directory type paths end in a "/".
            gold_dir += os.sep
        elif not (path.isfile() or path.isdir()):
            raise GoldenFileGenerationError(
                tarfile=current_archive, invalid=path)
        golden_set.add(gold_dir)
    return golden_set


def update_golden(updated_path, golden):
    """For presentation only. Never execute the cmd output programmatically because
    it may present a security exploit."""
    return "cp \"{}\" \"{}\"".format(
        os.path.abspath(updated_path), os.path.abspath(golden))


if __name__ == '__main__':
    sys.exit(main())
