# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess


def run_product_assembly(
        ffx_bin,
        product,
        input_bundles,
        legacy_bundle,
        outdir,
        extra_config=[],
        **kwargs):
    """
    Run `ffx assembly product ...` with appropriate configuration and arguments for host tests.

    Useful if you need to test assembly and assert that it will fail, or pass custom configuration
    to your invocation of the tool.

    Assumes that the script calling this function is an action or host test invoked such that
    cwd=root_build_dir.

    Optional arguments can be passed by name.
    """

    # assume we're in the root build dir right now and that is where we'll find ffx env
    root_build_dir = os.getcwd()
    ffx_env_path = "./.ffx.env"

    # imitate the configuration in //src/developer/ffx/build/ffx_action.gni
    base_config = [
        "analytics.disabled=true",
        "assembly_enabled=true",
        "sdk.root=" + root_build_dir,
        "sdk.type=in-tree",
        "sdk.module=host_tools.modular",
    ]

    args = [ffx_bin]
    for c in base_config + extra_config:
        args += ["--config", c]
    args += [
        "--env",
        ffx_env_path,
        "assembly",
        "product",
        "--product",
        product,
        "--input-bundles-dir",
        input_bundles,
        "--legacy-bundle",
        legacy_bundle,
        "--outdir",
        outdir,
    ]

    for arg_name, value in kwargs.items():
        args.append("--" + arg_name.replace('_', '-'))
        args.append(value)

    return subprocess.run(args, capture_output=True)
