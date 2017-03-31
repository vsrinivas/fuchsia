#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

BUILD_PATH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path += [os.path.join(BUILD_PATH, "third_party/pytoml-0.1.11")]
import pytoml as toml


def create_base_directory(file):
    path = os.path.dirname(file)
    try:
        os.makedirs(path)
    except os.error:
        # Already existed
        pass


def main():
    parser = argparse.ArgumentParser("Compiles a Rust crate")
    parser.add_argument("--type",
                        help="Type of artifact to produce",
                        required=True,
                        choices=["lib", "bin"])
    parser.add_argument("--name",
                        help="Name of the artifact to produce",
                        required=True)
    parser.add_argument("--out-dir",
                        help="Path to the output directory",
                        required=True)
    parser.add_argument("--gen-dir",
                        help="Path to the generated source directory",
                        required=True)
    parser.add_argument("--crate-root",
                        help="Path to the crate root",
                        required=True)
    parser.add_argument("--cargo",
                        help="Path to the cargo tool",
                        required=True)
    parser.add_argument("--linker",
                        help="Path to the Rust linker",
                        required=False)
    parser.add_argument("--rustc",
                        help="Path to the rustc binary",
                        required=True)
    parser.add_argument("--target-triple",
                        help="Compilation target",
                        required=True)
    args = parser.parse_args()

    env = os.environ.copy()
    if args.linker is not None:
        env["CARGO_TARGET_%s_LINKER" % args.target_triple.replace("-", "_").upper()] = args.linker
    env["CARGO_TARGET_DIR"] = args.out_dir
    env["RUSTC"] = args.rustc

    # Generate Cargo.toml.
    original_manifest = os.path.join(args.crate_root, "Cargo.toml")
    generated_manifest = os.path.join(args.gen_dir, "Cargo.toml")
    create_base_directory(generated_manifest)
    with open(original_manifest, "r") as manifest:
        config = toml.load(manifest)
        base = None
        if args.type == "bin":
            if "bin" not in config:
                raise Exception("Missing [[bin]] section in manifest")
            for bin in config["bin"]:
                if "name" in bin and bin["name"] == args.name:
                    base = bin
                    break
            if base is None:
                raise Exception("Could not find binary named %s" % args.name)
        if args.type == "lib":
            if "lib" not in config:
                raise Exception("Missing [lib] section in manifest")
            lib = config["lib"]
            if "name" not in lib or lib["name"] != args.name:
                raise Exception("Could not find library named %s" % args.name)
            base = lib
        # Rewrite the artifact's entry point so that it can be located by
        # reading the generated manifest file.
        if "path" not in base:
            raise Exception("Need to specify entry point for %s" % args.name)
        relative_path = base["path"]
        new_path = os.path.join(args.crate_root, relative_path)
        base["path"] = new_path
        with open(generated_manifest, "w") as generated_config:
            toml.dump(generated_config, config)

    call_args = [
        args.cargo,
        "build",
        "--target=%s" % args.target_triple,
        # Unfortunately, this option also freezes the lockfile meaning it cannot
        # be generated.
        # TODO(pylaligand): find a way to disable network access only or remove.
        # "--frozen",  # Prohibit network access.
        "-q",  # Silence stdout.
    ]
    if args.type == "lib":
        call_args.append("--lib")
    if args.type == "bin":
        call_args.extend(["--bin", args.name])
    return subprocess.call(call_args, env=env, cwd=args.gen_dir)


if __name__ == '__main__':
    sys.exit(main())
