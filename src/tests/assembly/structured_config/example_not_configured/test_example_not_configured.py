# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import pathlib
import subprocess
import sys

from run_assembly import run_product_assembly

EXAMPLE_ENABLED_FLAG = "assembly_example_enabled"


def main():
    parser = argparse.ArgumentParser(
        description=
        "Ensure that ffx assembly product fails with missing structured config."
    )
    parser.add_argument(
        "--ffx-bin",
        type=pathlib.Path,
        required=True,
        help="Path to ffx binary.")
    parser.add_argument(
        "--product-assembly-config",
        type=pathlib.Path,
        required=True,
        help="Path to product assembly configuration input.")
    parser.add_argument(
        "--input-bundles-dir",
        type=pathlib.Path,
        required=True,
        help="Path to input bundles directory.")
    parser.add_argument(
        "--outdir",
        type=pathlib.Path,
        required=True,
        help="Path to output directory.")
    parser.add_argument(
        "--stamp",
        type=pathlib.Path,
        required=True,
        help="Path to stampfile for telling ninja we're done.")
    args = parser.parse_args()

    output = run_product_assembly(
        ffx_bin=args.ffx_bin,
        product=args.product_assembly_config,
        input_bundles=args.input_bundles_dir,
        outdir=args.outdir)
    stdout, stderr = (
        output.stdout.decode("UTF-8"), output.stderr.decode("UTF-8"))

    test_failed = False
    if output.returncode == 0:
        test_failed = True
        print("Expected non-zero return code.")

    if EXAMPLE_ENABLED_FLAG not in stderr:
        test_failed = True
        print(
            f"Expected to find `{EXAMPLE_ENABLED_FLAG}` in stderr but did not.")

    if test_failed:
        print("Test failed!")
        print(f"Actual return code: {output.returncode}")
        print(f"Actual stdout:\n{stdout}")
        print(f"Actual stderr:\n{stderr}")
        sys.exit(1)
    else:
        with open(args.stamp, 'w') as f:
            pass  # creates the file
