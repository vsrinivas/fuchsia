#!/usr/bin/env python3
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Updates the Fuchsia platform version.
"""

import argparse
import json
import os
import re
import secrets
import shutil
import sys

from pathlib import Path

PLATFORM_VERSION_PATH = "build/config/fuchsia/platform_version.json"
VERSION_HISTORY_PATH = "sdk/version_history.json"
FIDL_COMPATIBILITY_DOC_PATH = "docs/development/testing/ctf/fidl_api_compatibility_testing.md"


def update_platform_version(fuchsia_api_level):
    """Updates platform_version.json to set the in_development_api_level to the given
    Fuchsia API level.
    """
    try:
        with open(PLATFORM_VERSION_PATH, "r+") as f:
            platform_version = json.load(f)
            platform_version["in_development_api_level"] = fuchsia_api_level
            f.seek(0)
            json.dump(platform_version, f)
            f.truncate()
        return True
    except FileNotFoundError:
        print(
            """error: Unable to open '{path}'.
Did you run this script from the root of the source tree?""".format(
                path=PLATFORM_VERSION_PATH),
            file=sys.stderr)
        return False


def update_fidl_compatibility_doc(fuchsia_api_level):
    """Updates fidl_api_compatibility_testing.md given the in-development API level."""
    try:
        with open(FIDL_COMPATIBILITY_DOC_PATH, "r+") as f:
            old_content = f.read()
            new_content = re.sub(
                r"\{% set in_development_api_level = \d+ %\}",
                f"{{% set in_development_api_level = {fuchsia_api_level} %}}",
                old_content)
            f.seek(0)
            f.write(new_content)
            f.truncate()
        return True
    except FileNotFoundError:
        print(
            """error: Unable to open '{path}'.
Did you run this script from the root of the source tree?""".format(
                path=FIDL_COMPATIBILITY_DOC_PATH),
            file=sys.stderr)
        return False


def generate_random_abi_revision():
    """Generates a random ABI revision.

    ABI revisions are hex encodings of 64-bit, unsigned integeres.
    """
    return '0x{abi_revision}'.format(abi_revision=secrets.token_hex(8).upper())


def update_version_history(fuchsia_api_level):
    """Updates version_history.json to include the given Fuchsia API level.

    The ABI revision for this API level is set to a new random value that has not
    been used before.
    """
    try:
        with open(VERSION_HISTORY_PATH, "r+") as f:
            version_history = json.load(f)
            versions = version_history['data']['versions']
            if [version for version in versions
                    if version['api_level'] == str(fuchsia_api_level)]:
                print(
                    "error: Fuchsia API level {fuchsia_api_level} is already defined."
                    .format(fuchsia_api_level=fuchsia_api_level),
                    file=sys.stderr)
                return False
            abi_revision = generate_random_abi_revision()
            while [version for version in versions
                   if version['abi_revision'] == abi_revision]:
                abi_revision = generate_random_abi_revision()
            versions.append(
                {
                    'api_level': str(fuchsia_api_level),
                    'abi_revision': abi_revision,
                })
            f.seek(0)
            json.dump(version_history, f, indent=4)
            f.truncate()
            return True
    except FileNotFoundError:
        print(
            """error: Unable to open '{path}'.
Did you run this script from the root of the source tree?""".format(
                path=VERSION_HISTORY_PATH),
            file=sys.stderr)
        return False


def move_owners_file(root_build_dir, fuchsia_api_level):
    """Helper function for copying golden files. It accomplishes the following:
    1. Overrides //sdk/history/OWNERS in //sdk/history/N/ allowing a wider set of reviewers.
    2. Reverts //sdk/history/N-1/  back to using //sdk/history/OWNERS, now that N-1 is a 
       supported API level.

    """
    root = join_path("sdk", "history")
    src = join_path(root, str(fuchsia_api_level - 1), "OWNERS")
    dst = join_path(root, str(fuchsia_api_level))

    try:
        os.mkdir(dst)
    except Exception as e:
        print(f"os.mkdir({dst}) failed: {e}")
        return False

    try:
        print(f"copying {src} to {dst}")
        shutil.move(src, dst)
    except Exception as e:
        print(f"shutil.move({src}, {dst}) failed: {e}")
        return False
    return True


def copy_compatibility_test_goldens(root_build_dir, fuchsia_api_level):
    """Updates the golden files used for compatibility testing".

    This assumes a clean build with:
      fx set core.x64 --with //sdk:compatibility_testing_goldens

    Any files that can't be copied are logged and must be updated manually.
    """
    goldens_manifest = os.path.join(
        root_build_dir, "compatibility_testing_goldens.json")

    with open(goldens_manifest) as f:
        for entry in json.load(f):
            src = join_path(root_build_dir, entry["src"])
            dst = join_path(root_build_dir, entry["dst"])
            try:
                print(f"copying {src} to {dst}")
                shutil.copyfile(src, dst)
            except Exception as e:
                print(f"failed to copy {src} to {dst}: {e}")
                return False
    return True


def join_path(root_dir, *paths):
    """Returns absolute path """
    return os.path.abspath(os.path.join(root_dir, *paths))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fuchsia-api-level", type=int, required=True)
    parser.add_argument("--update-goldens", type=bool, default=False)
    parser.add_argument("--root-build-dir", type=str, default="out/default")
    args = parser.parse_args()

    if not update_version_history(args.fuchsia_api_level):
        return 1

    if not update_platform_version(args.fuchsia_api_level):
        return 1

    if not update_fidl_compatibility_doc(args.fuchsia_api_level):
        return 1

    if args.update_goldens:
        if not move_owners_file(args.root_build_dir, args.fuchsia_api_level):
            return 1
        if not copy_compatibility_test_goldens(args.root_build_dir,
                                               args.fuchsia_api_level):
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
