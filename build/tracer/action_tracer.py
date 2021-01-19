#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import dataclasses
import os
import subprocess
import sys
from typing import FrozenSet, Iterable, Sequence, Tuple


def check_access_line(
        op: str, path: str, allowed_reads: FrozenSet[str],
        allowed_writes: FrozenSet[str]) -> Sequence[Tuple[str, str]]:
    """Validates a file system access against a set of allowed accesses.

    Args:
      op: operation code in [rwdtm]
        See: https://github.com/jacereda/fsatrace#output-format
      path: file(s) accessed.
        Only the 'm' move operation contains two paths: "destination|source".
      allowed_reads: set of allowed read paths.
      allowed_writes: set of allowed write paths.

    Returns:
      0 to 2 access violations in the form (op, path).
    """
    if op == "r":
        if path not in allowed_reads:
            return [("read", path)]
    elif op in {"w", "d", "t"}:
        if path not in allowed_writes:
            return [("write", path)]
    elif op == "m":
        # path: "destination|source" (both are considered writes)
        return [
            ("write", path)
            for path in path.split("|")
            if path not in allowed_writes
        ]
    return []


@dataclasses.dataclass
class AccessTraceChecker(object):
    """Validates a sequence of file system accesses against constraints.
    """

    # Accesses outside of this path prefix are not checked.
    # An empty string will cause the checker to validate all accesses.
    required_path_prefix: str = ""

    # These affixes will be ignored for read/write checks.
    ignored_prefixes: FrozenSet[str] = dataclasses.field(default_factory=set)
    ignored_suffixes: FrozenSet[str] = dataclasses.field(default_factory=set)
    ignored_path_parts: FrozenSet[str] = dataclasses.field(default_factory=set)

    # These are explicitly allowed accesses that are checked once the
    # above criteria are met.
    allowed_reads: FrozenSet[str] = dataclasses.field(default_factory=set)
    allowed_writes: FrozenSet[str] = dataclasses.field(default_factory=set)

    def check_accesses(self,
                       accesses: Iterable[str]) -> Sequence[Tuple[str, str]]:
        """Checks a sequence of access against constraints.

        Accesses outside of required_path_prefix are ignored.
        Accesses to paths that match any of the ignored_* properties will be ignored.
        All other accesses are checked against allowed_reads and allowed_writes.

        Args:
          accesses: lines in fsatrace output format.
            See: https://github.com/jacereda/fsatrace#output-format
            The sequence of accesses matters when tracking moves.

        Returns:
          access violations (in the order they were encountered)
        """
        unexpected_accesses = []
        for access in accesses:
            op, sep, path = access.partition("|")
            if not sep == "|":
                # Not a trace line, ignore
                continue
            if not path.startswith(self.required_path_prefix):
                # Outside of root, ignore
                continue
            if any(path.startswith(ignored)
                   for ignored in self.ignored_prefixes):
                continue
            if any(path.endswith(ignored) for ignored in self.ignored_suffixes):
                continue
            if any(part for part in path.split(os.path.sep)
                   if part in self.ignored_path_parts):
                continue
            unexpected_accesses.extend(
                check_access_line(
                    op, path, self.allowed_reads, self.allowed_writes))

        # TODO(fangism): check for missing must_write accesses
        # TODO(fangism): track state of files across moves
        return unexpected_accesses


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
    allowed_writes = {os.path.abspath(path) for path in args.outputs}

    depfile_deps = []
    if args.depfile:
        allowed_writes.add(os.path.abspath(args.depfile))
        with open(args.depfile, "r") as f:
            depfile_deps += [
                line.partition(":")[0]
                for line in f.read().strip().splitlines()
            ]

    allowed_reads = {
        os.path.abspath(path)
        for path in [args.script] + args.inputs + args.sources + depfile_deps
    } | allowed_writes
    # TODO(fangism): prevent inputs from being written/touched/moved.
    # Changes to the input may confuse any timestamp-based build system,
    # and introduce race conditions among multiple reader rules.

    if args.response_file_name:
        allowed_reads.add(os.path.abspath(response_file_name))

    # Limit most access checks to files under src_root.
    src_root = os.path.dirname(os.path.dirname(os.getcwd()))

    # Paths that are ignored
    ignored_prefixes = {
        # Allow actions to access prebuilts that are not declared as inputs
        # (until we fix all instances of this)
        os.path.join(src_root, "prebuilt"),
    }
    ignored_suffixes = {
        # Allow actions to access Python code such as via imports
        # TODO(fangism): validate python imports under source control more precisely
        ".py",
        # TODO(shayba): remove hack below for response files
        #".rsp",
    }
    # TODO(fangism): for suffixes that we always ignore for writing, such as safe
    # or intended side-effect byproducts, make sure no declared inputs ever match them.
    ignored_path_parts = {
        # Python creates these directories with bytecode caches
        "__pycache__",
    }

    raw_trace = ""
    with open(args.trace_output, "r") as trace:
        raw_trace = trace.read()

    # Verify the filesystem access trace of the inner action
    checker = AccessTraceChecker(
        required_path_prefix=src_root,
        ignored_prefixes=ignored_prefixes,
        ignored_suffixes=ignored_suffixes,
        ignored_path_parts=ignored_path_parts,
        allowed_reads=allowed_reads,
        allowed_writes=allowed_writes)
    unexpected_accesses = checker.check_accesses(raw_trace.splitlines())

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
