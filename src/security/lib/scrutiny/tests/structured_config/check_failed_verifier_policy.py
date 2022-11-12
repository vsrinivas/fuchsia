# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import pathlib
import subprocess
import unittest


def main():
    parser = argparse.ArgumentParser(
        description="Check that a 'bad' config is rejected.")
    parser.add_argument(
        "--ffx-bin",
        type=pathlib.Path,
        required=True,
        help="Path to the ffx binary.")
    parser.add_argument(
        "--policy",
        type=pathlib.Path,
        required=True,
        help="Path to JSON5 policy file which should produce errors.")
    parser.add_argument(
        "--failed-url",
        type=str,
        required=True,
        help="Component URL which should cause a policy failure.")
    parser.add_argument(
        "--failed-key",
        type=str,
        required=True,
        help="Component config key which should cause a policy failure.")
    parser.add_argument(
        "--depfile",
        type=pathlib.Path,
        required=True,
        help="Path to ninja depfile to write.")
    parser.add_argument(
        "--update-package",
        type=pathlib.Path,
        required=True,
        help="Path to the update package manifest.")
    parser.add_argument(
        "--blobfs", nargs="+", default=[], help="Paths to blobfs block images.")
    args = parser.parse_args()

    # Assume we're in the root build dir right now and that is where we'll find ffx env.
    root_build_dir = os.getcwd()
    ffx_env_path = "./.ffx.env"

    # Imitate the configuration in //src/developer/ffx/build/ffx_action.gni.
    base_config = [
        "analytics.disabled=true",
        "sdk.root=" + root_build_dir,
        "sdk.type=in-tree",
        "sdk.module=host_tools.modular",
    ]

    ffx_args = [args.ffx_bin]
    for c in base_config:
        ffx_args += ["--config", c]
    ffx_args += [
        "--env",
        ffx_env_path,
        "scrutiny",
        "verify",
        "structured-config",
        "--policy",
        args.policy,
        "--build-path",
        root_build_dir,
        "--update",
        args.update_package,
    ]

    for blobfs in args.blobfs:
        ffx_args += ["--blobfs", blobfs]

    test = unittest.TestCase()

    output = subprocess.run(ffx_args, capture_output=True)
    test.assertNotEqual(
        output.returncode, 0, "ffx scrutiny verify must have failed")

    stderr = output.stderr.decode('UTF-8')
    test.assertIn(
        args.failed_url, stderr, "error message must contain failed URL")
    test.assertIn(
        args.failed_key, stderr, "error message must contain failed config key")
