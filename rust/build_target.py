#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import itertools
import json
import os
import string
import subprocess
import sys

ROOT_PATH = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
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


# Gathers build metadata from the given dependencies.
def gather_dependency_infos(root_gen_dir, deps):
    result = []
    for dep in deps:
        path, name = get_target(dep)
        base_path = os.path.join(root_gen_dir, path, "%s.rust" % name)
        # Read the information attached to the target.
        info_path = os.path.join(base_path, "%s.info.toml" % name)
        with open(info_path, "r") as info_file:
            result.append(toml.load(info_file))
    return result


# Write some metadata about the target.
def write_target_info(label, gen_dir, package_name, native_libs,
                      has_generated_code=True):
    _, target_name = get_target(label)
    # Note: gen_dir already contains the "target.rust" directory.
    info_path = os.path.join(gen_dir, "%s.info.toml" % target_name)
    create_base_directory(info_path)
    info = {
        "name": package_name,
        "native_libs": native_libs,
        "base_path": gen_dir,
        "has_generated_code": has_generated_code,
    }
    with open(info_path, "w") as info_file:
        toml.dump(info, info_file)


# Returns the list of native libs inherited from the given dependencies.
def extract_native_libs(dependency_infos):
    all_libs = itertools.chain.from_iterable(map(lambda i: i["native_libs"],
                                                 dependency_infos))
    return list(set(all_libs))


# Writes a cargo config file.
def write_cargo_config(path, vendor_directory, target_triple, shared_libs_root,
                       native_libs):
    create_base_directory(path)
    config = {
        "source": {
            "crates-io": {
                "registry": "https://github.com/rust-lang/crates.io-index",
                "replace-with": "vendored-sources"
            },
            "vendored-sources": {
                "directory": vendor_directory
            },
        },
    }

    if native_libs is not None:
        config["target"] = {}
        config["target"][target_triple] = {}
        for lib in native_libs:
            config["target"][target_triple][lib] = {
                "rustc-link-search": [ shared_libs_root ],
                "rustc-link-lib": [ lib ],
                "root": shared_libs_root,
            }

    with open(path, "w") as config_file:
        toml.dump(config, config_file)


# Fixes the target path in the given depfile.
def fix_depfile(depfile_path, base_path):
    with open(depfile_path, "r+") as depfile:
        content = depfile.read()
        content_split = content.split(': ', 1)
        target_path = content_split[0]
        adjusted_target_path = os.path.relpath(target_path, start=base_path)
        new_content = "%s: %s" % (adjusted_target_path, content_split[1])
        depfile.seek(0)
        depfile.write(new_content)
        depfile.truncate()


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
    parser.add_argument("--root-out-dir",
                        help="Path to the root output directory",
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
    parser.add_argument("--release",
                        help="Build in release mode",
                        action="store_true")
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
    parser.add_argument("--shared-libs-root",
                        help="Path to the location of shared libraries",
                        required=True)
    parser.add_argument("--with-tests",
                        help="Whether to generate unit tests too",
                        action="store_true")
    args = parser.parse_args()

    dependency_infos = gather_dependency_infos(args.root_gen_dir, args.deps)

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
    package_name = None
    with open(original_manifest, "r") as manifest:
        config = toml.load(manifest)
        package_name = config["package"]["name"]
        default_name = package_name.replace("-", "_")

        # Update the path to the sources.
        base = None
        if args.type == "bin":
            if "bin" not in config:
                # Use the defaults.
                config["bin"] = [{
                    "name": package_name,
                    "path": "src/main.rs"
                }]
            for bin in config["bin"]:
                if "name" in bin and bin["name"] == args.name:
                    base = bin
                    break
            if base is None:
                raise Exception("Could not find binary named %s" % args.name)
        if args.type == "lib":
            if "lib" not in config:
                # Use the defaults.
                config["lib"] = {
                    "name": default_name,
                    "path": "src/lib.rs"
                }
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
        base["path"] = os.path.relpath(new_path, args.gen_dir)

        # Add or edit dependency sections for local deps.
        if "dependencies" not in config:
            config["dependencies"] = {}
        dependencies = config["dependencies"]
        for info in dependency_infos:
            if not info["has_generated_code"]:
                # This is a third-party dependency, cargo already knows how to
                # find it.
                continue
            artifact_name = info["name"]
            base_path = info["base_path"]
            if artifact_name not in dependencies:
                dependencies[artifact_name] = {}
            dependencies[artifact_name]["path"] = os.path.relpath(base_path,
                                                                  args.gen_dir)

        # Write the complete manifest.
        with open(generated_manifest, "w") as generated_config:
            toml.dump(config, generated_config)

    # Gather the set of native libraries that will need to be linked.
    native_libs = extract_native_libs(dependency_infos)

    if args.type == "lib":
        # Write a file mapping target name to some metadata about the target.
        # This will be used to set up dependencies.
        write_target_info(args.label, args.gen_dir, package_name, native_libs)

    # Write a config file to allow cargo to find the vendored crates.
    config_path = os.path.join(args.gen_dir, ".cargo", "config")
    write_cargo_config(config_path, args.vendor_directory, args.target_triple,
                       args.shared_libs_root, native_libs)

    if args.type == "lib":
        # Since the generated .rlib artifact won't actually be used (for now),
        # just do syntax checking and avoid generating it.
        build_command = "check"
    else:
        build_command = "build"

    # Remove any existing Cargo.lock file since it may need to be generated
    # again if third-party crates have been updated.
    try:
        os.remove(os.path.join(args.gen_dir, "Cargo.lock"))
    except OSError:
        pass

    call_args = [
        args.cargo,
        build_command,
        "--target=%s" % args.target_triple,
        # Unfortunately, this option also freezes the lockfile meaning it cannot
        # be generated.
        # TODO(pylaligand): find a way to disable network access only or remove.
        # "--frozen",  # Prohibit network access.
        "-q",  # Silence stdout.
    ]
    if args.release:
        call_args.append("--release")
    if args.type == "lib":
        call_args.append("--lib")
    if args.type == "bin":
        call_args.extend(["--bin", args.name])
    subprocess.check_call(call_args, env=env, cwd=args.gen_dir)

    # Fix the depfile manually until a flag gets added to cargo to tweak the
    # base path for targets.
    # Note: out_dir already contains the "target.rust" directory.
    output_name = args.name
    if args.type == "lib":
        output_name = "lib%s" % args.name
    build_type = "release" if args.release else "debug"
    depfile_path = os.path.join(args.out_dir, args.target_triple, build_type,
                                "%s.d" % output_name)
    fix_depfile(depfile_path, args.root_out_dir)

    if args.with_tests:
        test_args = list(call_args)
        test_args[1] = "test"
        test_args.append("--no-run")
        test_args.append("--message-format=json")
        messages = subprocess.check_output(test_args, env=env, cwd=args.gen_dir)
        generated_test_path = None
        for line in messages.splitlines():
            data = json.loads(line)
            if (data["profile"]["test"]):
              generated_test_path = data["filenames"][0]
              break
        if not generated_test_path:
            raise Exception("Unable to locate resulting test file")
        dest_test_path = os.path.join(args.out_dir,
                                      "%s-%s-test" % (args.name, args.type))
        if os.path.islink(dest_test_path):
            os.unlink(dest_test_path)
        os.symlink(generated_test_path, dest_test_path)

    return 0


if __name__ == '__main__':
    sys.exit(main())
