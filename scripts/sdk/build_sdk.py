#!/usr/bin/env python3

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
This script serves as the entry point for building the merged GN SDK.
Due to complexities around the merging process, this can't be easily expressed
as a single GN target. This script thus serves as the canonical build script,
providing a single reproducible process.
"""

import atexit
import argparse
import json
import os
import shutil
import stat
import subprocess
import tempfile

import merger.merge as merge
import gn.generate as generate

REPOSITORY_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", ".."))
FINT_PARAMS_DIR = os.path.join(
    REPOSITORY_ROOT, "integration", "infra", "config", "generated", "turquoise",
    "fint_params", "global.ci")
SUPPORTED_ARCHITECTURES = ["x64", "arm64"]
SDK_ARCHIVE_JSON = "sdk_archives.json"
FINT_BOOTSTRAP_SCRIPT = os.path.join(
    REPOSITORY_ROOT, "tools", "integration", "bootstrap.sh")
FINT_OUTPUT = os.getenv("FX_CACHE_DIR", default="/tmp/fint")
FINT_BOOTSTRAP_COMMAND = [FINT_BOOTSTRAP_SCRIPT, "-o", FINT_OUTPUT]

FINT_CONTEXT_TEMPLATE = """checkout_dir: "{checkout_dir}"
build_dir: "{build_dir}"
"""

FINT_PARAMS_MAP = {
    "x64":
        os.path.join(
            FINT_PARAMS_DIR, "sdk-google-linux-x64-build_only.textproto"),
    "arm64":
        os.path.join(
            FINT_PARAMS_DIR, "sdk-google-linux-arm64-build_only.textproto")
}


def determine_build_dir(context_file):
    """
    Parses a context file to determine the build directory.
    See the context template above for an example.
    Infra builds will have more fields in the file.
    """
    with open(context_file) as context:
        for line in context.readlines():
            if line.startswith("build_dir: "):
                return line.lstrip("build_dir: ").strip(' "\n')


def build_for_arch(arch, fint, fint_params_files):
    """
    Does the build for an architecture, as defined by the fint param files.
    """
    print("Building {}".format(arch))

    # Read the build dir from the context file here for simplicity
    # even though sometimes it was set by the script in the first place.
    build_dir = determine_build_dir(fint_params_files["context"])
    subprocess.check_call(
        [
            fint, "-log-level=error", "set", "-static",
            fint_params_files["static"], "-context",
            fint_params_files["context"]
        ])
    subprocess.check_call(
        [
            fint, "-log-level=error", "build", "-static",
            fint_params_files["static"], "-context",
            fint_params_files["context"]
        ])

    # The sdk archives file contains several entries since some SDKs build
    # others as intermediates, but only one should still be present.
    with open(os.path.join(build_dir, SDK_ARCHIVE_JSON)) as sdk_archive_json:
        sdk_archives = json.load(sdk_archive_json)
    for sdk in sdk_archives:
        if os.path.isfile(os.path.join(build_dir, sdk["path"])):
            return os.path.join(build_dir, sdk["path"])


def bootstrap_fint():
    subprocess.check_call(FINT_BOOTSTRAP_COMMAND)
    return FINT_OUTPUT


def generate_context(arch):
    """
    Generates an appropriate fint context file for a given architecture.
    """
    temp_context = "/tmp/context-{arch}".format(arch=arch)
    build_dir = os.path.join(REPOSITORY_ROOT, "out/release-{}".format(arch))
    with open(temp_context, "w") as context:
        context.write(
            FINT_CONTEXT_TEMPLATE.format(
                checkout_dir=REPOSITORY_ROOT, build_dir=build_dir))
    atexit.register(lambda: os.remove(temp_context))
    return temp_context


def build_fint_params(args):
    """
    Determines the appropriate fint params for a given architecture.
    Based on hardcoded values when architecture is provided as an argument.
    """
    fint_params = {}
    if args.fint_config:
        fint_params = json.loads(args.fint_config)
    elif args.fint_params_path:
        fint_params["custom"] = {
            "static": args.fint_params_path,
            "context": generate_context("custom")
        }
    elif args.arch:
        for arch in args.arch:
            fint_params[arch] = {
                "static": FINT_PARAMS_MAP[arch],
                "context": generate_context(arch)
            }
    return fint_params


def overwrite_even_if_RO(src, dst):
    """
    Copy function that tries to overwrite the destination file
    even if it's not writable.
    This is to support the use case where the same output directory is used
    more than once, as some files are read only.
    """
    if not os.access(dst, os.W_OK):
        os.chmod(dst, stat.S_IWUSR)
    shutil.copy2(src, dst)


def main():
    parser = argparse.ArgumentParser(
        description="Creates a GN SDK for a given architecture.")
    parser.add_argument(
        "--output",
        help="Output file path. If this file already exists, it will be replaced"
    )
    parser.add_argument(
        "--output-dir",
        help="Output SDK directory. Will overwrite existing files if present")
    parser.add_argument(
        "--fint-path", help="Path to fint", default=bootstrap_fint())
    build_params = parser.add_mutually_exclusive_group(required=True)
    build_params.add_argument(
        "--fint-config",
        help=
        "JSON with architectures and fint params. Use format {{arch: {{'static': static_path, 'context': context_path}}}}"
    )
    build_params.add_argument(
        "--arch",
        nargs="+",
        choices=SUPPORTED_ARCHITECTURES,
        help="Target architectures")
    build_params.add_argument(
        "--fint-params-path",
        help="Path of a single fint params file to use for this build")
    args = parser.parse_args()

    if not (args.output or args.output_dir):
        print(
            "Either an output file or an output directory should be specified.")
        return

    original_dir = os.getcwd()

    # Switch to the Fuchsia tree and build the SDKs.
    os.chdir(REPOSITORY_ROOT)

    output_dirs = []
    fint_params = build_fint_params(args)

    for arch in fint_params:
        sdk = build_for_arch(arch, args.fint_path, fint_params[arch])
        output_dirs.append(sdk)

    with tempfile.TemporaryDirectory() as tempdir:
        if len(output_dirs) > 1:
            print("Merging")
            primary = output_dirs[0]
            for sdk in output_dirs[1:]:
                merge.main(
                    [
                        "--first-archive",
                        primary,
                        "--second-archive",
                        sdk,
                        "--output-archive",
                        os.path.join(
                            os.path.dirname(sdk), "arch_merged.tar.gz"),
                    ])
                primary = os.path.join(
                    os.path.dirname(sdk), "arch_merged.tar.gz")

            # Process the Core SDK tarball to generate the GN SDK.
            print("Generating GN SDK")
            if (generate.main([
                    "--archive",
                    primary,
                    "--output",
                    tempdir,
            ])) != 0:
                print("Error - Failed to generate GN build files")
                return 1

        else:
            print("Generating GN SDK")
            if (generate.main([
                    "--archive",
                    output_dirs[0],
                    "--output",
                    tempdir,
            ])) != 0:
                print("Error - Failed to generate GN build files")
                return 1

        if args.output:
            os.makedirs(os.path.dirname(args.output), exist_ok=True)
            generate.create_archive(args.output, tempdir)

        if args.output_dir:
            if not os.path.exists(args.output_dir) and not os.path.isfile(
                    args.output_dir):
                os.makedirs(args.output_dir)
            shutil.copytree(
                tempdir,
                args.output_dir,
                copy_function=overwrite_even_if_RO,
                dirs_exist_ok=True)


# Clean up.
    os.chdir(original_dir)

if __name__ == "__main__":
    main()
