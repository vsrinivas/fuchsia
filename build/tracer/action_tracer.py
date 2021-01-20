#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import dataclasses
import enum
import itertools
import os
import shlex
import subprocess
import sys
from typing import AbstractSet, Collection, FrozenSet, Iterable, Sequence, Tuple


class FileAccessType(enum.Enum):
    # String values for readable diagnostics.
    READ = enum.auto()
    WRITE = enum.auto()
    DELETE = enum.auto()


@dataclasses.dataclass
class FSAccess(object):
    """Represents a single file system access."""
    # One of: "read", "write" (covers touch), "delete" (covers move-from)
    op: FileAccessType
    # The path accessed
    path: str

    # TODO(fangism): for diagnostic purposes, we may want a copy of the fsatrace
    # line from which this access came.

    def __repr__(self):
        return f"({self.op} {self.path})"

    def should_check(
            self,
            required_path_prefix: str = "",
            ignored_prefixes: FrozenSet[str] = {},
            ignored_suffixes: FrozenSet[str] = {},
            ignored_path_parts: FrozenSet[str] = {}) -> bool:
        """Predicate function use to filter out FSAccesses.

        Args:
          required_path_prefix: Accesses outside of this path prefix are not checked.
            An empty string means: check this access.
          ignored_prefixes: don't check accesses whose path starts with these prefixes.
          ignored_suffixes: don't check accesses whose path ends with these suffixes.
          ignored_path_parts: don't check accesses whose path *components* match any of
            these names exactly.

        Returns:
          true if this access should be checked.
        """
        if not self.path.startswith(required_path_prefix):
            return False
        if any(self.path.startswith(ignored) for ignored in ignored_prefixes):
            return False
        if any(self.path.endswith(ignored) for ignored in ignored_suffixes):
            return False
        if any(part for part in self.path.split(os.path.sep)
               if part in ignored_path_parts):
            return False
        return True

    def allowed(
            self, allowed_reads: FrozenSet[str],
            allowed_writes: FrozenSet[str]) -> bool:
        """Validates a file system access against a set of allowed accesses.

        Args:
          allowed_reads: set of allowed read paths.
          allowed_writes: set of allowed write paths.

        Returns:
          True if this access is allowed.
        """
        if self.op == FileAccessType.READ:
            return self.path in allowed_reads
        elif self.op == FileAccessType.WRITE:
            return self.path in allowed_writes
        elif self.op == FileAccessType.DELETE:
            # TODO(fangism): separate out forbidded_deletes
            return self.path in allowed_writes
        raise ValueError(f"Unknown operation: {self.op}")


# Factory functions for making FSAccess objects.
def Read(path: str):
    return FSAccess(FileAccessType.READ, path)


def Write(path: str):
    return FSAccess(FileAccessType.WRITE, path)


def Delete(path: str):
    return FSAccess(FileAccessType.DELETE, path)


def _parse_fsatrace_line(fsatrace_line: str) -> Iterable[FSAccess]:
    """Parses an output line from fsatrace into a stream of FSAccesses.

    See: https://github.com/jacereda/fsatrace#output-format
    Moves are split into two operations: delete source, write destination

    Args:
      fsatrace_line: one line of trace from fsatrace

    Yields:
      0 to 2 FSAccess objects.
    """
    # ignore any lines that do not parse
    op, sep, path = fsatrace_line.partition("|")
    if not sep == "|":
        return

    # op: operation code in [rwdtm]
    if op == "r":
        yield Read(path)
    elif op in {"w", "t"}:
        yield Write(path)
    elif op == "d":
        yield Delete(path)
    elif op == "m":
        # path: "destination|source"
        # The source is deleted, and the destination is written.
        dest, sep, source = path.partition("|")
        if not sep == "|":
            raise ValueError("Malformed move line: " + fsatrace_line)
        yield Delete(source)
        yield Write(dest)


def parse_fsatrace_output(fsatrace_lines: Iterable[str]) -> Iterable[FSAccess]:
    """Returns a stream of FSAccess objects."""
    return itertools.chain.from_iterable(
        _parse_fsatrace_line(line) for line in fsatrace_lines)


@dataclasses.dataclass
class AccessTraceChecker(object):
    """Validates a sequence of file system accesses against constraints.
    """

    # These are explicitly allowed accesses that are checked once the
    # above criteria are met.
    allowed_reads: FrozenSet[str] = dataclasses.field(default_factory=set)
    allowed_writes: FrozenSet[str] = dataclasses.field(default_factory=set)

    def check_accesses(self,
                       accesses: Iterable[FSAccess]) -> Sequence[FSAccess]:
        """Checks a sequence of access against constraints.

        Accesses outside of required_path_prefix are ignored.
        Accesses to paths that match any of the ignored_* properties will be ignored.
        All other accesses are checked against allowed_reads and allowed_writes.

        Args:
          accesses: stream of file-system accesses

        Returns:
          access violations (in the order they were encountered)
        """
        unexpected_accesses = [
            access for access in accesses
            if not access.allowed(self.allowed_reads, self.allowed_writes)
        ]

        # TODO(fangism): track state of files across moves
        return unexpected_accesses


def check_missing_writes(
        accesses: Iterable[FSAccess],
        required_writes: FrozenSet[str]) -> AbstractSet[str]:
    """Tracks sequence of access to verify that required files are written.

    Args:
      accesses: file-system accesses.
      required_writes: paths that are expected to be written.

    Returns:
      Subset of required_writes that were not fulfilled.
    """
    missing_writes = required_writes.copy()
    for access in accesses:
        if access.op == FileAccessType.WRITE and access.path in missing_writes:
            missing_writes.remove(access.path)
        elif access.op == FileAccessType.DELETE and access.path in required_writes:
            missing_writes.add(access.path)

    return missing_writes


@dataclasses.dataclass
class DepEdges(object):
    ins: FrozenSet[str] = dataclasses.field(default_factory=set)
    outs: FrozenSet[str] = dataclasses.field(default_factory=set)

    def abspaths(self) -> "DepEdges":
        return DepEdges(
            ins={os.path.abspath(p) for p in self.ins},
            outs={os.path.abspath(p) for p in self.outs})


def parse_dep_edges(depfile_line: str) -> DepEdges:
    """Parse a single line of a depfile.

    This assumes that all depfile entries are formatted onto a single line.
    TODO(fangism): support more generalized forms of input, e.g. multi-line.
      See https://github.com/ninja-build/ninja/blob/master/src/depfile_parser_test.cc

    Args:
      depfile_line: has the form "OUTPUT: INPUT INPUT ..."

    Returns:
      A DepEdges object represending a dependency between inputs and outputs.

    Raises:
      ValueError if unable to parse dependency entry.
    """
    out, sep, ins = depfile_line.strip().partition(":")
    if sep != ":":
        raise ValueError("Failed to parse depfile entry:\n" + depfile_line)
    return DepEdges(ins=set(shlex.split(ins)), outs={out.strip()})


@dataclasses.dataclass
class DepFile(object):
    """DepFile represents a collection of dependency edges."""
    deps: Collection[DepEdges] = dataclasses.field(default_factory=list)

    @property
    def all_ins(self) -> AbstractSet[str]:
        """Returns a set of all dependency inputs."""
        return {f for dep in self.deps for f in dep.ins}

    @property
    def all_outs(self) -> AbstractSet[str]:
        """Returns a set of all dependency outputs."""
        return {f for dep in self.deps for f in dep.outs}


def parse_depfile(depfile_lines: Iterable[str]) -> DepFile:
    """Parses a depfile into a set of inputs and outputs.

    See https://github.com/ninja-build/ninja/blob/master/src/depfile_parser_test.cc
    for examples of format using Ninja syntax.

    Limitation: For now, assume one dep per line.
    TODO(fangism): ignore blank/comment lines
    """
    return DepFile(deps=[parse_dep_edges(line) for line in depfile_lines])


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

    # TODO(fangism): factor out logic for allowed/required calculation for
    # testability (before the ignored_* section).

    # Action is required to write outputs and depfile, if provided.
    required_writes = {os.path.abspath(path) for path in args.outputs}

    # Paths that the action is allowed to write.
    # Actions may touch files other than their listed outputs.
    allowed_writes = required_writes.copy()

    depfile_ins = []
    if args.depfile and os.path.exists(args.depfile):
        # Writing the depfile is not required (yet), but allowed.
        allowed_writes.add(os.path.abspath(args.depfile))
        with open(args.depfile, "r") as f:
            depfile = parse_depfile(f)

        depfile_ins = depfile.all_ins
        allowed_writes.update(depfile.all_ins)  # TODO(fangism): allowed_reads?
        allowed_writes.update(depfile.all_outs)

    # Everything writeable is readable.
    allowed_reads = {
        os.path.abspath(path)
        for path in [args.script] + args.inputs + args.sources + depfile_ins
    } | allowed_writes

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
    }
    ignored_path_parts = {
        # Python creates these directories with bytecode caches
        "__pycache__",
    }
    # TODO(fangism): for suffixes that we always ignore for writing, such as safe
    # or intended side-effect byproducts, make sure no declared inputs ever match them.

    raw_trace = ""
    with open(args.trace_output, "r") as trace:
        raw_trace = trace.read()

    # Parse trace file.
    all_accesses = parse_fsatrace_output(raw_trace.splitlines())

    # Filter out access we don't want to track.
    filtered_accesses = [
        access for access in all_accesses if access.should_check(
            # Ignore accesses that fall outside of the source root.
            required_path_prefix=src_root,
            ignored_prefixes=ignored_prefixes,
            ignored_suffixes=ignored_suffixes,
            ignored_path_parts=ignored_path_parts,
        )
    ]

    # Verify the filesystem access trace.
    checker = AccessTraceChecker(
        allowed_reads=allowed_reads,
        allowed_writes=allowed_writes,
    )
    unexpected_accesses = checker.check_accesses(filtered_accesses)

    # Verify that outputs are written as promised.
    missing_writes = check_missing_writes(filtered_accesses, required_writes)

    # Check for overall correctness, print diagnostics,
    # and exit with the right code.
    exit_code = 0
    if unexpected_accesses:
        accesses_formatted = "\n".join(
            f"{access}" for access in unexpected_accesses)
        print(
            f"""
Unexpected file accesses building {args.label}, following the order they are accessed:
{accesses_formatted}

Full access trace:
{raw_trace}

See: https://fuchsia.dev/fuchsia-src/development/build/hermetic_actions

""",
            file=sys.stderr,
        )
        exit_code = 1

    if missing_writes:
        required_writes_formatted = "\n".join(required_writes)
        missing_writes_formatted = "\n".join(missing_writes)
        print(
            f"""
Not all outputs of {args.label} were written or touched, which can cause subsequent
build invocations to re-execute actions due to a missing file or old timestamp.
Writes to the following files are missing:

Required writes:
{required_writes_formatted}

Missing writes:
{missing_writes_formatted}

See: https://fuchsia.dev/fuchsia-src/development/build/ninja_no_op

""",
            file=sys.stderr,
        )
        exit_code = 1

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
