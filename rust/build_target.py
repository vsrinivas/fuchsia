#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string
import subprocess
import sys

BUILD_PATH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path += [os.path.join(BUILD_PATH, "third_party/pytoml-0.1.11")]
import pytoml as toml


# Creates the directory containing the given file.
def create_base_directory(file):
    path = os.path.dirname(file)
    try:
        os.makedirs(path)
    except os.error:
        # Already existed.
        pass


# Extracts a (path, name) tuple from the given build label.
def get_target(label):
    if not label.startswith("//"):
        raise Exception("Expected label to start with //, got %s" % label)
    base = label[2:]
    separator_index = string.rfind(base, ":")
    if separator_index >= 0:
        name = base[separator_index+1:]
        path = base[:separator_index]
    else:
        name = base[base.rfind("/")+1:]
        path = base
    return path, name


# Writes a cargo config file.
def write_cargo_config(path, vendor_directory):
    create_base_directory(path)
    config = '''[source.crates-io]
registry = 'https://github.com/rust-lang/crates.io-index'
replace-with = 'vendored-sources'

[source.vendored-sources]
directory = '%s'
''' % vendor_directory
    with open(path, "w") as config_file:
        config_file.write(config)


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
                        help="Path to the target's generated source directory",
                        required=True)
    parser.add_argument("--root-gen-dir",
                        help="Path to the root gen directory",
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
    parser.add_argument("--label",
                        help="Label of the target to build",
                        required=True)
    parser.add_argument("--cmake-dir",
                        help="Path to the directory containing cmake",
                        required=True)
    parser.add_argument("--vendor-directory",
                        help="Path to the vendored crates",
                        required=True)
    parser.add_argument("--deps",
                        help="List of dependencies",
                        nargs="*")
    args = parser.parse_args()

    env = os.environ.copy()
    if args.linker is not None:
        env["CARGO_TARGET_%s_LINKER" % args.target_triple.replace("-", "_").upper()] = args.linker
    env["CARGO_TARGET_DIR"] = args.out_dir
    env["RUSTC"] = args.rustc
    env["PATH"] = "%s:%s" % (env["PATH"], args.cmake_dir)

    # Generate Cargo.toml.
    original_manifest = os.path.join(args.crate_root, "Cargo.toml")
    generated_manifest = os.path.join(args.gen_dir, "Cargo.toml")
    create_base_directory(generated_manifest)
    with open(original_manifest, "r") as manifest:
        config = toml.load(manifest)

        # Update the path to the sources.
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

        if "dependencies" not in config:
            config["dependencies"] = {}
        # Add dependency sections for local deps.
        for dep in args.deps:
            path, name = get_target(dep)
            # Read the name of the Rust artifact.
            artifact_path = os.path.join(args.root_gen_dir, path,
                                         "%s.rust_name" % name)
            with open(artifact_path, "r") as artifact_file:
                artifact_name = artifact_file.read()
            config["dependencies"][artifact_name] = {
                "path": os.path.join(args.root_gen_dir, path)
            }

        # Write the complete manifest.
        with open(generated_manifest, "w") as generated_config:
            toml.dump(generated_config, config)

    if args.type == "lib":
        # Write a file mapping target name to Rust artifact name.
        # This will be used to set up dependencies.
        _, target_name = get_target(args.label)
        name_path = os.path.join(args.gen_dir, "%s.rust_name" % target_name)
        create_base_directory(name_path)
        with open(name_path, "w") as name_file:
            name_file.write(args.name)

    # Write a config file to allow cargo to find the vendored crates.
    config_path = os.path.join(args.gen_dir, ".cargo", "config")
    write_cargo_config(config_path, args.vendor_directory)

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
    return_code = subprocess.call(call_args, env=env, cwd=args.gen_dir)
    if return_code != 0:
        return return_code

    # Make binaries accessible from a standard location in the output directory.
    if args.type == "bin":
        symlink = os.path.join(args.out_dir, args.name)
        # TODO(pylaligand): adjust debug/release when necessary.
        bin = os.path.join(args.out_dir, args.target_triple, "debug", args.name)
        if os.path.islink(symlink):
            os.unlink(symlink)
        os.symlink(bin, symlink)

    return 0


if __name__ == '__main__':
    sys.exit(main())
