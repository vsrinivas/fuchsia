#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Traces a GN action and enforces strict inputs/outputs",
        argument_default=[],
    )
    parser.add_argument(
        "--label", required=True, help="The wrapped target's label")
    parser.add_argument(
        "--trace-output", required=True, help="Where to store the trace")
    parser.add_argument(
        "--target-type",
        choices=["action", "action_foreach"],
        default="action",
        help="Type of target being wrapped",
    )
    parser.add_argument("--script", required=True, help="action#script")
    parser.add_argument("--response-file-name", help="action#script")
    parser.add_argument("--inputs", nargs="*", help="action#inputs")
    parser.add_argument("--sources", nargs="*", help="action#sources")
    parser.add_argument("--outputs", nargs="*", help="action#outputs")
    parser.add_argument("--depfile", help="action#depfile")
    parser.add_argument("args", nargs="*", help="action#args")
    args = parser.parse_args()

    # Ensure trace_output directory exists
    trace_output_dir = os.path.dirname(args.trace_output)
    os.makedirs(trace_output_dir, exist_ok=True)

    # TODO(shayba): make this work without assuming `fsatrace` in path
    retval = subprocess.call(
        ["fsatrace", "rwmdt", args.trace_output, "--", args.script] + args.args)

    # If inner action failed that's a build error, don't bother with the trace.
    if retval != 0:
        return retval

    # Scripts with known issues
    # TODO(shayba): file bugs for the suppressions below
    ignored_scripts = [
        "sdk_build_id.py",
        # TODO(shayba): it's not the wrapper script that's the problem but some
        # of its usages. Refine the suppression or just fix the underlying
        # issues.
        "gn_script_wrapper.py",
        # When using `/bin/ln -f`, a temporary file may be created in the
        # target directory. This will register as a write to a non-output file.
        # TODO(shayba): address this somehow.
        "ln",
        # fxbug.dev/61771
        # "analysis_options.yaml",
    ]
    if os.path.basename(args.script) in ignored_scripts:
        return 0

    # `compiled_action()` programs with known issues
    # TODO(shayba): file bugs for the suppressions below
    ignored_compiled_actions = [
        # fxbug.dev/61770
        "banjo_bin",
        "strings_to_json",
    ]
    if args.script == "../../build/gn_run_binary.sh":
        if os.path.basename(args.args[1]) in ignored_compiled_actions:
            return 0

    # Paths that the action is allowed to access
    allowed_write = {os.path.abspath(path) for path in args.outputs}

    depfile_deps = []
    if args.depfile:
        allowed_write.add(os.path.abspath(args.depfile))
        with open(args.depfile, "r") as f:
            depfile_deps += [
                line.partition(":")[0]
                for line in f.read().strip().splitlines()
            ]

    allowed_read = {
        os.path.abspath(path)
        for path in [args.script] + args.inputs + args.sources + depfile_deps
    } | allowed_write
    if args.response_file_name:
        allowed_read.add(os.path.abspath(response_file_name))

    # Paths that are ignored
    src_root = os.path.dirname(os.path.dirname(os.getcwd()))
    ignored_prefix = {
        # Allow actions to access prebuilts that are not declared as inputs
        # (until we fix all instances of this)
        os.path.join(src_root, "prebuilt"),
    }
    ignored_postfix = {
        # Allow actions to access Python code such as via imports
        ".py",
        # Allow actions to access Python compiled bytecode
        ".pyc",
        # TODO(shayba): remove hack below for response files
        #".rsp",
    }

    raw_trace = ""
    with open(args.trace_output, "r") as trace:
        raw_trace = trace.read()

    # Verify the filesystem access trace of the inner action
    # See: https://github.com/jacereda/fsatrace#output-format
    unexpected_accesses = []
    for line in raw_trace.splitlines():
        if not line[1] == "|":
            # Not a trace line, ignore
            continue
        path = line[2:]
        if not path.startswith(src_root):
            # Outside of root, ignore
            continue
        if any(path.startswith(ignored) for ignored in ignored_prefix):
            continue
        if any(path.endswith(ignored) for ignored in ignored_postfix):
            continue
        op = line[0]
        if op == "r":
            if path not in allowed_read:
                unexpected_accesses.append(("read", path))
        elif op in {"w", "d", "t"}:
            if path not in allowed_write:
                unexpected_accesses.append(("write", path))
        elif op == "m":
            unexpected_accesses.extend(
                ("write", path)
                for path in path.split("|")
                if path not in allowed_write)

    if not unexpected_accesses:
        return 0

    accesses = "\n".join(f"{op} {path}" for (op, path) in unexpected_accesses)
    print(
        f"""
Unexpected file accesses building {args.label}, following the order they are accessed:
{accesses}

Full access trace:
{raw_trace}

See: https://fuchsia.dev/fuchsia-src/development/build/hermetic_actions

""",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
