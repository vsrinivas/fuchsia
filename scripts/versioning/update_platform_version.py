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
import sys
import json
import secrets

PLATFORM_VERSION_PATH = "build/config/fuchsia/platform_version.json"
VERSION_HISTORY_PATH = "sdk/version_history.json"


def update_platform_version(fuchsia_api_level):
    """Updates platform_version.json to set the current_fuchsia_api_level to the given
    Fuchsia API level.
    """
    try:
        with open(PLATFORM_VERSION_PATH, "w") as f:
            json.dump(fuchsia_api_level, f)
    except FileNotFoundError:
        print(
            """error: Unable to open '{path}'.
Did you run this script from the root of the source tree?""".format(
                path=PLATFORM_VERSION_PATH),
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fuchsia-api-level", type=int, required=True)
    args = parser.parse_args()

    if not update_version_history(args.fuchsia_api_level):
        return 1

    if not update_platform_version(args.fuchsia_api_level):
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
