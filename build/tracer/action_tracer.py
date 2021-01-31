#!/usr/bin/env python3
"""Validates file system accesses of a subprocess command.

This uses a traced exection wrapper (fsatrace) to invoke a command,
captures a trace of file system {read,write} operations, and validates
those access against constraints such as declared inputs and outputs.
"""
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import enum
import itertools
import os
import shlex
import subprocess
import sys
from typing import AbstractSet, Any, Callable, Collection, FrozenSet, Iterable, Optional, Sequence, TextIO, Tuple
import dataclasses


def _partition(
        iterable: Iterable[Any],
        predicate: Callable[[Any],
                            bool]) -> Tuple[Sequence[Any], Sequence[Any]]:
    """Splits sequence into two sequences based on predicate function."""
    trues = []
    falses = []
    for item in iterable:
        if predicate(item):
            trues.append(item)
        else:
            falses.append(item)
    return trues, falses


class FileAccessType(enum.Enum):
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
    if sep != "|":
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
        if sep != "|":
            raise ValueError("Malformed move line: " + fsatrace_line)
        yield Delete(source)
        yield Write(dest)


def parse_fsatrace_output(fsatrace_lines: Iterable[str]) -> Iterable[FSAccess]:
    """Returns a stream of FSAccess objects."""
    return itertools.chain.from_iterable(
        _parse_fsatrace_line(line) for line in fsatrace_lines)


def _abspaths(container: Iterable[str]) -> AbstractSet[str]:
    return {os.path.abspath(f) for f in container}


@dataclasses.dataclass
class AccessConstraints(object):
    """Set of file system accesses constraints."""
    allowed_reads: FrozenSet[str] = dataclasses.field(default_factory=set)
    allowed_writes: FrozenSet[str] = dataclasses.field(default_factory=set)
    required_writes: FrozenSet[str] = dataclasses.field(default_factory=set)


@dataclasses.dataclass
class Action(object):
    """Represents a set of parameters of a single build action."""
    script: str
    inputs: Sequence[str] = dataclasses.field(default_factory=list)
    outputs: Collection[str] = dataclasses.field(default_factory=list)
    sources: Sequence[str] = dataclasses.field(default_factory=list)
    depfile: Optional[str] = None
    response_file_name: Optional[str] = None

    def access_constraints(
            self, writeable_depfile_inputs=False) -> AccessConstraints:
        """Build AccessConstraints from action attributes."""
        # Action is required to write outputs and depfile, if provided.
        required_writes = {path for path in self.outputs}

        # Paths that the action is allowed to write.
        # Actions may touch files other than their listed outputs.
        allowed_writes = required_writes.copy()

        allowed_reads = {
            path for path in [self.script] + self.inputs + self.sources
        }

        if self.depfile and os.path.exists(self.depfile):
            # Writing the depfile is not required (yet), but allowed.
            allowed_writes.add(self.depfile)
            with open(self.depfile, "r") as f:
                depfile = parse_depfile(f)

            if (writeable_depfile_inputs):
                allowed_writes.update(depfile.all_ins)
            else:
                allowed_reads.update(depfile.all_ins)
            allowed_writes.update(depfile.all_outs)

        # Everything writeable is readable.
        allowed_reads.update(allowed_writes)

        if self.response_file_name:
            allowed_reads.add(self.response_file_name)

        return AccessConstraints(
            allowed_reads=_abspaths(allowed_reads),
            allowed_writes=_abspaths(allowed_writes),
            required_writes=_abspaths(required_writes))


def check_access_permissions(
        accesses: Iterable[FSAccess],
        allowed_reads: FrozenSet[str] = {},
        allowed_writes: FrozenSet[str] = {}) -> Sequence[FSAccess]:
    """Checks a sequence of accesses against permission constraints.

    Args:
      accesses: stream of file-system accesses.
      allowed_reads: set of files that are allowed to be read.
      allowed_writes: set of files that are allowed to be written.

    Returns:
      access violations (in the order they were encountered)
    """
    unexpected_accesses = [
        access for access in accesses
        if not access.allowed(allowed_reads, allowed_writes)
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


def actually_read_files(accesses: Iterable[FSAccess]) -> AbstractSet[str]:
    """Returns subset of files that were actually used/read."""
    return {
        access.path for access in accesses if access.op == FileAccessType.READ
    }


@dataclasses.dataclass
class DepEdges(object):
    ins: FrozenSet[str] = dataclasses.field(default_factory=set)
    outs: FrozenSet[str] = dataclasses.field(default_factory=set)

    def abspaths(self) -> "DepEdges":
        return DepEdges(ins=_abspaths(self.ins), outs=_abspaths(self.outs))


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

    Args:
      depfile_lines: lines from a depfile

    Returns:
      DepFile object, collection of dependencies.
    """
    return DepFile(deps=[parse_dep_edges(line) for line in depfile_lines])


def _verbose_path(path: str) -> str:
    """When any symlinks are followed, show this."""
    realpath = os.path.realpath(path)
    if path != realpath:
        return path + " -> " + realpath
    return path


@dataclasses.dataclass
class OutputDiagnostics(object):
    """Just a structure to capture results of diagnosing outputs."""
    required_writes: FrozenSet[str] = dataclasses.field(default_factory=set)
    nonexistent_outputs: FrozenSet[str] = dataclasses.field(default_factory=set)
    # If there are stale_outputs, then it must have been compared against a
    # newest_input.
    newest_input: Optional[str] = None
    stale_outputs: FrozenSet[str] = dataclasses.field(default_factory=set)

    @property
    def has_findings(self):
        return self.nonexistent_outputs or self.stale_outputs

    def print_findings(self, stream: TextIO):
        """Prints human-readable diagnostics.

        Args:
          stream: a file stream, like sys.stderr.
        """
        required_writes_formatted = "\n".join(
            _verbose_path(f) for f in self.required_writes)
        print(
            f"""
Required writes:
{required_writes_formatted}
""", file=stream)
        if self.nonexistent_outputs:
            nonexistent_outputs_formatted = "\n".join(
                _verbose_path(f) for f in self.nonexistent_outputs)
            print(
                f"""
Missing outputs:
{nonexistent_outputs_formatted}
""",
                file=stream)

        if self.stale_outputs:
            stale_outputs_formatted = "\n".join(
                _verbose_path(f) for f in self.stale_outputs)
            print(
                f"""
Stale outputs: (older than newest input: {self.newest_input})
{stale_outputs_formatted}
""",
                file=stream)


def realpath_ctime(path: str) -> int:
    """Follow symlinks before getting ctime.

    This reflects Ninja's behavior of using `stat()` instead of `lstat()`
    on symlinks.

    Args:
      path: file or symlink

    Returns:
      ctime of the realpath of path.
    """
    return os.path.getctime(os.path.realpath(path))


def diagnose_stale_outputs(
        accesses: Iterable[FSAccess],
        access_constraints: AccessConstraints) -> OutputDiagnostics:
    """Analyzes access stream for missing writes.

    Also compares timestamps of inputs relative to outputs
    to determine staleness.

    Args:
      accesses: trace of file system accesses.
      access_constraints: access that may/must[not] occur.

    Returns:
      Structure of findings, including missing/stale outputs.
    """
    # Verify that outputs are written as promised.
    missing_writes = check_missing_writes(
        accesses, access_constraints.required_writes)

    # Distinguish stale from nonexistent output files.
    untouched_outputs, nonexistent_outputs = _partition(
        missing_writes, os.path.exists)

    # Check that timestamps relative to inputs (allowed_reads) are newer,
    # in which case, not-writing outputs is acceptable.
    # Determines file use based on the `accesses` trace,
    # not the stat() filesystem function.
    read_files = actually_read_files(accesses)
    # Ignore allowed-but-unused inputs.
    used_inputs = access_constraints.allowed_reads.intersection(read_files)

    # Compare timestamps vs. newest input to find stale outputs.
    stale_outputs = set()
    newest_input = None
    if used_inputs and untouched_outputs:
        newest_input = max(used_inputs, key=realpath_ctime)
        # Filter out untouched outputs that are still newer than used inputs.
        input_timestamp = realpath_ctime(newest_input)
        stale_outputs = {
            out for out in untouched_outputs
            if realpath_ctime(out) < input_timestamp
        }
    return OutputDiagnostics(
        required_writes=access_constraints.required_writes,
        nonexistent_outputs=set(nonexistent_outputs),
        newest_input=newest_input,
        stale_outputs=stale_outputs)


def main_arg_parser() -> argparse.ArgumentParser:
    """Construct the argument parser, called by main()."""
    parser = argparse.ArgumentParser(
        description="Traces a GN action and enforces strict inputs/outputs",
        argument_default=[],
    )
    parser.add_argument(
        "--fsatrace-path",
        default="fsatrace",
        help=
        "Path to fsatrace binary.  If omitted, it will search for one in PATH.")
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

    parser.add_argument(
        "--failed-check-status",
        type=int,
        default=1,
        help=
        "On failing tracing checks, exit with this code.  Use 0 to report findings without failing.",
    )

    # Want --foo (default:True) and --no-foo (False).
    # This is ugly, trying to emulate argparse.BooleanOptionalAction,
    # which isn't available until Python 3.9.
    parser.add_argument(
        "--check-access-permissions",
        action="store_true",
        default=True,
        help="Check permissions on file reads and writes")
    parser.add_argument(
        "--no-check-access-permissions",
        action="store_false",
        dest="check_access_permissions")

    parser.add_argument(
        "--check-output-freshness",
        action="store_true",
        default=True,
        help="Check timestamp freshness of declared outputs")
    parser.add_argument(
        "--no-check-output-freshness",
        action="store_false",
        dest="check_output_freshness")

    parser.add_argument(
        "--writeable-depfile-inputs",
        action="store_true",
        default=True,  # Goal: False (remove this flag entirely)
        help="Allow writes to inputs found in depfiles.")
    parser.add_argument(
        "--no-writeable-depfile-inputs",
        action="store_false",
        dest="writeable_depfile_inputs")

    # Positional args are the command to run and trace.
    parser.add_argument("args", nargs="*", help="action#args")
    return parser


def main():
    parser = main_arg_parser()
    args = parser.parse_args()

    # Ensure trace_output directory exists
    trace_output_dir = os.path.dirname(args.trace_output)
    os.makedirs(trace_output_dir, exist_ok=True)

    os.environ["FSAT_BUF_SIZE"] = "5000000"
    retval = subprocess.call(
        [
            args.fsatrace_path,
            "erwmdt",
            args.trace_output,
            "--",
            args.script,
        ] + args.args)

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

    # Compute constraints from action properties (from args).
    action = Action(
        script=args.script,
        inputs=args.inputs,
        outputs=args.outputs,
        sources=args.sources,
        depfile=args.depfile,
        response_file_name=args.response_file_name)
    access_constraints = action.access_constraints(
        writeable_depfile_inputs=args.writeable_depfile_inputs)

    # Limit most access checks to files under src_root.
    src_root = os.path.dirname(os.path.dirname(os.getcwd()))

    # Paths that are ignored
    ignored_prefixes = {
        # Allow actions to access prebuilts that are not declared as inputs
        # (until we fix all instances of this)
        os.path.join(src_root, "prebuilt"),
        # Allow actions to run `git` commands.
        # Actions can set certain refs under .git as inputs to trigger on
        # relevant changes to git. However fully predicting what files will be
        # accessed by certain git commands used in the build is not viable, it's
        # not necessarily stable and doesn't make a good contract.
        os.path.join(src_root, ".git"),
        os.path.join(src_root, "integration", ".git"),
    }
    ignored_suffixes = {
        # Allow actions to access Python code such as via imports
        # TODO(fangism): validate python imports under source control more
        # precisely
        ".py",
    }
    ignored_path_parts = {
        # Python creates these directories with bytecode caches
        "__pycache__",
    }
    # TODO(fangism): for suffixes that we always ignore for writing, such as
    # safe or intended side-effect byproducts, make sure no declared inputs ever
    # match them.

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

    # Check for overall correctness, print diagnostics,
    # and exit with the right code.
    exit_code = 0
    if args.check_access_permissions:
        # Verify the filesystem access trace.
        unexpected_accesses = check_access_permissions(
            filtered_accesses,
            allowed_reads=access_constraints.allowed_reads,
            allowed_writes=access_constraints.allowed_writes)

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
                file=sys.stderr)
            exit_code = args.failed_check_status

    if args.check_output_freshness:
        output_diagnostics = diagnose_stale_outputs(
            accesses=filtered_accesses, access_constraints=access_constraints)
        if output_diagnostics.has_findings:

            print(
                f"""
Not all outputs of {args.label} were written or touched, which can cause subsequent
build invocations to re-execute actions due to a missing file or old timestamp.
""",
                file=sys.stderr)
            output_diagnostics.print_findings(sys.stderr)
            print(
                f"""
Full access trace:
{raw_trace}

See: https://fuchsia.dev/fuchsia-src/development/build/ninja_no_op

""",
                file=sys.stderr)
            exit_code = args.failed_check_status

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
