# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import pathlib
import subprocess
import sys

from run_assembly import run_product_assembly


def main():
    parser = argparse.ArgumentParser(
        description="Run ffx assembly with the provided arguments.")
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
        "--legacy-bundle",
        type=pathlib.Path,
        required=True,
        help="Path to the legacy input bundle manifest.")
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
    parser.add_argument(
        "--additional-packages-path",
        type=pathlib.Path,
        required=False,
        help="Path to additional packages configuration.")
    parser.add_argument(
        "--config",
        action="append",
        required=False,
        help="Package config arguments.")
    args = parser.parse_args()

    kwargs = {}
    if args.additional_packages_path:
        kwargs['additional_packages_path'] = args.additional_packages_path
    if args.config:
        kwargs['extra_config'] = args.config

    output = run_product_assembly(
        ffx_bin=args.ffx_bin,
        product=args.product_assembly_config,
        input_bundles=args.input_bundles_dir,
        legacy_bundle=args.legacy_bundle,
        outdir=args.outdir,
        **kwargs)
    if output.returncode != 0:
        print('command failed! stderr:')
        print(output.stderr.decode('UTF-8'))
        sys.exit(1)
    with open(args.stamp, 'w') as f:
        pass  # creates the file
