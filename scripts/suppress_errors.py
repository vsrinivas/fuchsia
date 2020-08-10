#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Example usage:
# $ fx set ...
# $ scripts/suppress_errors.py\
# --error=-Wconversion
# --config="//build/config:Wno-conversion"

import argparse
import fileinput
import os
import re
import subprocess
import sys


def run_command(command):
    return subprocess.check_output(
        command, stderr=subprocess.STDOUT, encoding="utf8")


def main():
    parser = argparse.ArgumentParser(
        description="Adds a given config to all compile targets with "
        "a given compiler error")
    parser.add_argument("--error", required=True, help="Compiler error marker")
    parser.add_argument("--config", required=True, help="Config to add")
    parser.add_argument(
        "--zircon", help="//zircon build (ZN)", action="store_true")
    args = parser.parse_args()
    error = args.error
    config = args.config
    zircon = args.zircon

    # Harvest all compilation errors
    print("Building...")
    try:
        # On error continue building to discover all failures
        run_command(["fx", "build", "-k0"])
        print("Build successful!")
        return 0
    except subprocess.CalledProcessError as e:
        build_out = e.output
    error_regex = re.compile(
        "(?:../)*([^:]*):\d*:\d*: error: .*" + re.escape(error))
    error_files = set()
    for line in build_out.splitlines():
        match = error_regex.match(line)
        if match:
            path = os.path.normpath(match.group(1))
            if zircon:
                path = "//" + os.path.relpath(path, "zircon")
            error_files.add(path)
    print("Sources with compilation errors:")
    print("\n".join(sorted(error_files)))
    print()
    if not error_files:
        return 0

    # Collect all BUILD.gn files with failing targets
    print("Resolving failing targets...")
    outdir = "out/default.zircon" if zircon else "out/default"
    refs_command = ["fx", "gn", "refs"]
    if zircon:
        refs_command += [
            "out/default.zircon",
            "--root=zircon",
            "--all-toolchains",
            "//:instrumented-ulib-redirect.asan(//public/gn/toolchain:user-arm64-clang)",
            "//:instrumented-ulib-redirect.asan(//public/gn/toolchain:user-x64-clang)",
        ]
    else:
        refs_command += ["out/default"]
    refs_command += sorted(error_files)
    try:
        refs_out = run_command(refs_command)
    except subprocess.CalledProcessError as e:
        print("Failed to resolve references!")
        print(e.output)
        return 1
    target_no_toolchain = re.compile("(\/\/[^:]*:\w*)(?:\(.*\))?")
    error_targets = set(
        target_no_toolchain.match(ref).group(1)
        for ref in refs_out.splitlines())
    print("Failing BUILD.gn files:")
    print("\n".join(sorted(error_targets)))
    print()

    # Fix failing targets
    ref_regex = re.compile("//([^:]*):([^.(]*).*")
    for target in sorted(error_targets):
        match = ref_regex.match(target)
        build_dir, target_name = match.groups()
        target_regex = re.compile(
            '^\s*\w*\("' + re.escape(target_name) + '"\) {')
        build_file = os.path.join(
            build_dir, "BUILD.gn" if not zircon else "BUILD.zircon.gn")
        # Format file before processing
        run_command(["fx", "format-code", "--files=" + build_file])
        print("Fixing", target)
        in_target = False
        config_printed = False
        in_configs = False
        curly_brace_depth = 0
        if zircon:
            build_file = os.path.join("zircon", build_file)
        secondary_build_file = os.path.join("build", "secondary", build_file)
        if os.path.exists(secondary_build_file):
            # Sometimes we put third_party BUILD.gn files in a shadow directory
            # to avoid conflicting with original BUILD.gn files.
            build_file = secondary_build_file
        start_configs_inline = re.compile('configs \+?= \[ "')
        start_configs = re.compile("configs \+?= \[")

        for line in fileinput.FileInput(build_file, inplace=True):
            curly_brace_depth += line.count("{") - line.count("}")
            assert curly_brace_depth >= 0
            if config_printed:
                # We already printed the config, keep running until we exit the target
                if curly_brace_depth == target_end_depth:
                    in_target = False
            elif not in_target:
                # Try to enter target
                in_target = bool(target_regex.match(line))
                if in_target:
                    target_end_depth = curly_brace_depth - 1
            elif curly_brace_depth > target_end_depth + 1:
                # Ignore inner blocks such as inner definitions and conditionals
                pass
            elif curly_brace_depth == target_end_depth:
                # Last chance to print config before exiting
                print('configs += [ "' + config + '" ]')
                config_printed = True
                in_target = False
            elif start_configs_inline.match(line) and config in line:
                config_printed = True
            elif start_configs_inline.match(line):
                line = line[:-3] + ', "' + config + '" ]\n'
                config_printed = True
            elif start_configs.match(line):
                in_configs = True
            elif in_configs and line.strip() == '"' + config + '",':
                in_configs = False
                config_printed = True
            elif in_configs and line.strip() == "]":
                print('"' + config + '",')
                in_configs = False
                config_printed = True
            print(line, end="")
            if config_printed and not in_target:
                # Reset for a possible redefinition of the same target
                # (e.g. within another conditional block)
                config_printed = False
        run_command(["fx", "format-code", "--files=" + build_file])

    return 0


if __name__ == "__main__":
    sys.exit(main())
