# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import pathlib
import subprocess
import sys

PACKAGE = "configured_by_assembly"
COMPONENT = "to_configure.cm"


def main():
    parser = argparse.ArgumentParser(
        description=
        "Ensure that configc validate-package fails with missing structured config."
    )
    parser.add_argument(
        "--configc-bin",
        type=pathlib.Path,
        required=True,
        help="Path to configc binary.")
    parser.add_argument(
        "--package",
        type=pathlib.Path,
        required=True,
        help="Path to package manifest.")
    args = parser.parse_args()

    output = subprocess.run(
        [
            args.configc_bin,
            "validate-package",
            args.package,
            "--stamp",
            "/dev/null",
        ],
        capture_output=True)
    stdout, stderr = (
        output.stdout.decode("UTF-8"), output.stderr.decode("UTF-8"))

    test_failed = False
    if output.returncode == 0:
        test_failed = True
        print("Expected non-zero return code.")

    if PACKAGE not in stderr:
        test_failed = True
        print(f"Expected to find {PACKAGE} in stderr.")

    if COMPONENT not in stderr:
        test_failed = True
        print(f"Expected to find {COMPONENT} in stderr.")

    if test_failed:
        print("Test failed!")
        print(f"Actual return code: {output.returncode}")
        print(f"Actual stdout:\n{stdout}")
        print(f"Actual stderr:\n{stderr}")
        sys.exit(1)
