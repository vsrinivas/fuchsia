# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A cli tool for use in Software Assembly development.

Particularly useful during the migration from GN to Product Assembly.
Currently Supports:
    1. Validating the contents of the following outputs across different builds
        1. Image Assembly Config
    2. Quickly stashing uncommitted changes and rerunning a clean build
    3. Persisting various files in a directory for later comparison
    4. Checking the contents of JSON files against a specific built commit-hash

This file offers a quick entrypoint and various convenience methods to
facilitate easier Product Assembly development.
"""

import argparse
import subprocess
from pathlib import Path
import json
import sys
from types import SimpleNamespace
from pprint import pformat
from typing import List
import logging
from contextlib import contextmanager
import textwrap

logger = logging.getLogger()
UNSTAGED = "unstaged"

FORMATTED_PARSER_DESCRIPTION = "\n".join(
    textwrap.wrap(
        """Validate that a move from the product definition file to an Assembly Input Bundle results
         in:""",
        width=100)) + "\n" + """
    1. The same number of targets listed in the image_assembly_config.json as compared with
    the same file prior to the move.
    2. A suspected change in path of the target or targets which have moved as defined in
    the image_assembly_config.json"""


def setup_path_variables(target_outdir, target="fuchsia"):
    """Configures various relevant (to Software Assembly) paths for use throughout the program
    """
    paths = SimpleNamespace()
    paths.ASSEMBLY_TARGET = target
    paths.TARGET_OUT_DIR = Path(f"{target_outdir}/obj/build/images/{target}")
    paths.FUCHSIA_DIR = target_outdir.parent.parent
    paths.ASSEMBLY_BUNDLES = Path(f"{paths.TARGET_OUT_DIR}/{target}")
    paths.GENDIR = Path(f"{paths.ASSEMBLY_BUNDLES}/gen")
    paths.LEGACY_ASSEMBLY_INPUT_BUNDLE = Path(
        f"{paths.ASSEMBLY_BUNDLES}/legacy")
    paths.LEGACY_AIB_MANIFEST = Path(
        f"{paths.LEGACY_ASSEMBLY_INPUT_BUNDLE}/assembly_config.json")
    paths.IMAGE_ASSEMBLY_CONFIG = Path(
        f"{paths.ASSEMBLY_BUNDLES}.image_assembly_config.json")
    paths.IMAGES_CONFIG = Path(f"{paths.ASSEMBLY_BUNDLES}.images_config.json")
    paths.IMAGE_ASSEMBLY_INPUTS = Path(
        f"{paths.ASSEMBLY_BUNDLES}.image_assembly_inputs")
    paths.BASE_PACKAGE_MANIFEST = Path(
        f"{paths.ASSEMBLY_BUNDLES}/base/package_manifest.json")
    paths.BOOTFS_PACKAGES = Path(f"{paths.GENDIR}/data/bootfs_packages")
    paths.STATIC_PACKAGES = Path(f"{paths.GENDIR}/legacy/data/static_packages")
    paths.CACHE_PACKAGES = Path(
        f"{paths.GENDIR}/legacy/data/cache_packages.json")
    paths.ASSEMBLY_MANIFEST = Path(f"{paths.ASSEMBLY_BUNDLES}/images.json")
    paths.ASSEMBLY_INPUT_BUNDLE_MANIFEST = Path(
        f"{paths.LEGACY_ASSEMBLY_INPUT_BUNDLE}.fini_manifest")
    return paths


def cli_args():
    """CLI Argparser setup"""
    parser = argparse.ArgumentParser(
        description=FORMATTED_PARSER_DESCRIPTION,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "-y",
        "--auto-confirm",
        action="store_true",
        required=False,
        default=False)

    parser.add_argument(
        "--commit-hash",
        required=False,
        help="An optional commit-hash prefix (8 or more digits) specifying which stored copy of the\
        image assembly config to compare against")
    args = parser.parse_args()

    return args


def confirm_desired_build_status():
    """Checks for user input to confirm that the build target is that which was desired.

    Prevents superfluous build efforts on incorrect targets, and prevents
    accidental comparisons to image assembly configs that are from the wrong
    build target
    """
    subprocess.run(["fx", "status"])
    ans = input("Would you like to proceed with this build target?:\n")
    if ans and ans[0] == "y":
        return True
    return False


def get_build_dir():
    """Returns the $FUCHSIA_DIR/OUT/$BUILD_TARGET directory as an importlib.Path"""
    result = subprocess.check_output(["fx", "get-build-dir"])
    return Path(result.decode("utf-8").strip())


def copy_config_to_tmp_dir(tmp_dir: Path, new_iac, stored_iac: Path,  commit_hash: str):
    """Copies a config file from the git HEAD to the tmp directory in a path that contains the
    commit hash as a prefix
    """
    # prevents overwriting file with commit hash prefix in the event of unstaged changes
    _copy_file(new_iac, stored_iac, overwrite=not _uncommitted_changes())
    return stored_iac


@contextmanager
def stash_uncommitted():
    """Temporarily stash uncommitted changes (if they exist) and return them when the context
    manager exits
    """
    stash = _uncommitted_changes()
    try:
        if stash:
            subprocess.run(["git", "stash", "save", '"Stashing during validate_gn_to_aib invocation"'])
        yield
    finally:
        if stash:
            subprocess.run(["git", "stash", "pop"])


def compare(old: Path, new: Path):
    """Compares two JSON files"""
    print(f"Comparing:\n - OLD: {old}\n - NEW: {new}\n")
    with open(old, "r") as old_iac_file, open(new, "r") as new_iac_file:
        old_iac = json.load(old_iac_file)
        new_iac = json.load(new_iac_file)
        _diff_dicts(old_iac, new_iac, [""])


def get_prev_iac_path(tmp_dir: Path, iac_name: str, commit_hash: str):
    """Gets a path to the previous iac.

    Optional commit hash allows for comparing against a historically saved
    commit, if it exists
    """
    return _get_path_to_historic_iac(tmp_dir, iac_name, commit_hash)


def _create_tmp_dir_if_not_exists(
        tmp_dir: Path = Path("/tmp/validate_gn_to_aib")):
    """Sets up persistent tmp directory if it doesn't already exist"""
    tmp_dir.mkdir(parents=True, exist_ok=True)
    return tmp_dir


def _get_head_commit_hash_prefix():
    """Returns the commit hash of the git HEAD"""
    result = subprocess.check_output(["git", "rev-parse", "HEAD"])
    return result.decode("utf-8").strip()[0:8]


def _copy_file(src, dst, overwrite=False):
    """Copies a src file to a target dst if the dst doesn't exist, or if the function is explicitly
    told to overwrite the existing file
    """
    dst.parent.mkdir(parents=True, exist_ok=True)
    if overwrite or not dst.exists():
        if overwrite:
            print("overwriting file")
        print(f"writing file to {dst}")
        dst.write_bytes(src.read_bytes())


def _get_path_to_historic_iac(tmp_dir, iac_name, commit_hash):
    return tmp_dir.joinpath(commit_hash, iac_name)


def _uncommitted_changes() -> bool:
    """Determines if the current git project has uncommitted changes"""
    # returns all files with uncommitted changes
    if subprocess.check_output(["git", "status", "--porcelain=v1"]):
        return True
    return False


def _diff_dicts(old, new, path: List):
    """Recursively checks the two JSON files for the expected discrepancies and logs them to stdout.

    NOTE: This function is not a full JSON differ and should not be used as
    such. It makes many
    assumptions about the two input files and will fail if those assumptions are
    not met.
    """
    for key in old.keys():
        path.append(key)
        if key not in new:
            raise Exception(f"Keys should not have changed. Found {key} was missing")
        if isinstance(old[key], dict):
            _diff_dicts(old[key], new[key], path + [key])
        elif isinstance(old[key], list):
            if len(old[key]) != len(new[key]):
                print(f"IAC at {'.'.join(path)} has different lengths")
                old_set = set(old[key])
                new_set = set(new[key])
                old_not_new = old_set.difference(new_set)
                new_not_old = new_set.difference(old_set)
                if old_not_new:
                    print("OLD IAC has the following values missing from NEW:")
                    print(old_not_new)
                if new_not_old:
                    print("NEW IAC has the following values missing from OLD:")
                    print(new_not_old)
            else:
                discrepencies = []
                for old_val, new_val in zip(old[key], new[key]):
                    if old_val != new_val:
                        discrepencies.append(f"OLD: {old_val}")
                        discrepencies.append(f"NEW: {new_val}")
                        discrepencies.append("")
                if discrepencies:
                    print(f"List at {'.'.join(path)} differs from the comparison")
                    list(map(print, discrepencies))

def attempt_clean_build(tmp_dir, outdir_iac, commit_hash):
    stored_iac_path = _get_path_to_historic_iac(tmp_dir, outdir_iac.name, commit_hash)
    if not stored_iac_path.exists():
        with stash_uncommitted():
            subprocess.run(["fx", "clean"])
            subprocess.run(["fx", "build"])
            # Creates a copy of the image assembly config in the tmp dir for the given HEAD commit
            # hash if one doesn't already exist.
            copy_config_to_tmp_dir(tmp_dir, outdir_iac, stored_iac_path, commit_hash)
    else:
        logger.info(f"The IAC from {commit_hash} already exists, so skipping the clean build.\n\
            If you'd like to trigger a clean build and rewrite the existing config at the given\
            hash, delete the file at {stored_iac_path}")

def main():
    def validate_commit_hash(commit_hash):
        if len(commit_hash) < 8 or tmp_dir.joinpath(
                commit_hash[0:8]) not in tmp_dir.iterdir():
            t = f"Must pass a commit hash of length >=8 that exists in tmp_dir. Valid options are:"
            sys.exit("\n".join(textwrap.wrap(t, width=100)) + "\n" +
                     pformat([path.name for path in tmp_dir.iterdir()]))

    args = cli_args()
    target_outdir = get_build_dir()
    paths = setup_path_variables(target_outdir)
    outdir_iac = paths.IMAGE_ASSEMBLY_CONFIG
    tmp_dir = _create_tmp_dir_if_not_exists()
    commit_hash = _get_head_commit_hash_prefix()

    if not args.auto_confirm:
        if not confirm_desired_build_status():
            sys.exit("Select the appropriate build with 'fx set' and try again")

    if args.commit_hash and validate_commit_hash(args.commit_hash):
        prev_iac = get_prev_iac_path(tmp_dir, outdir_iac.name,
                                args.commit_hash)
    else:
        prev_iac = get_prev_iac_path(tmp_dir, outdir_iac.name, commit_hash)

    # We want to run a clean build whenever the parent commit has changed and we don't have a valid
    # IAC for the parent commit stored in the tmp dir.
    attempt_clean_build(tmp_dir, outdir_iac, commit_hash)

    if not _uncommitted_changes():
        sys.exit(
            "Please make changes to the appropriate GN files and try again.")

    subprocess.run(["fx", "build"]) # again, this time with uncommitted changes
    compare(prev_iac, outdir_iac)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        logger.exception("")
