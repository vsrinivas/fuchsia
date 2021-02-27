#!/usr/bin/env python3
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Wraps a command so that its outputs are timestamp-fresh only if their contents change.

Every declared output is renamed with a temporary suffix in the command.
If the command succeeds, the temporary file is moved over the original declared output
if the output did not already exist or the contents are different.
This conditional move is done for every declared output that appears in the arguments list.

Assumptions:
  Output files are their own token in the command's arguments.
    Limitation: This won't attempt to find/rename outputs in "--flag=out1,out2" (yet),
      nor outputs whose names appear in some file.
  If x is a writeable path (output), then x.any_suffix is also writeable.
  Command being wrapped does not change behavior with the name of its output arguments.

If any of the above assumptions do not hold, then bypass wrapping.
"""

import argparse
import os
import shutil
import subprocess
import sys
from typing import Collection, FrozenSet, Sequence
import dataclasses


def files_match(file1: str, file2: str):
    """`diff`s two files, and forwards its exit status (0=same)."""
    # Silence "Files x and y differ" message.
    # TODO(fangism): can use faster diff-ing strategies, e.g. file size
    return subprocess.call(
        ["diff", "-q", file1, file2],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL) == 0


def move_if_different(src: str, dest: str, verbose: bool = False):
    if not os.path.exists(src):
        # Then original command failed to produce this file.
        return
    if not os.path.exists(dest) or not files_match(dest, src):
        if verbose:
            print(f"  === Updated: {dest}")
        shutil.move(src, dest)
    else:
        if verbose:
            print(f"  === Cached: {dest}")
        os.remove(src)


@dataclasses.dataclass
class TempFileTransform(object):
    """Represents a file name transform.

    temp_dir: Write temporary files in here.
      If blank, paths are relative to working directory.
    suffix: Add this suffix to temporary files, e.g. ".tmp".
      At least temp_dir or suffix must be non-blank.
    """
    temp_dir: str = ""
    suffix: str = ""

    @property
    def valid(self):
        return self.temp_dir or self.suffix

    def transform(self, path: str) -> str:
        return os.path.join(self.temp_dir, path + self.suffix)


@dataclasses.dataclass
class Action(object):
    """Represents a set of parameters of a single build action."""
    command: Sequence[str] = dataclasses.field(default_factory=list)
    outputs: FrozenSet[str] = dataclasses.field(default_factory=set)

    def run_cached(
            self,
            tempfile_transform: TempFileTransform,
            verbose: bool = False) -> int:
        """Runs a modified command and conditionally moves outputs in-place.

        Args:
          tempfile_transform: describes transformation to temporary file name.
          verbose: If True, print substituted command.
        """
        # keys: original file names, values: transformed temporary file names
        renamed_outputs = {}

        def replace_arg(arg):
            # TODO(fangism): lex a single arg into tokens, substitute, re-assemble
            #   This would support outputs like "--flag=out1,out2..."
            if arg in self.outputs:
                new_arg = tempfile_transform.transform(arg)
                renamed_outputs[arg] = new_arg
                return new_arg
            else:
                return arg

        # Rename output arguments with a suffix.
        substituted_command = [replace_arg(token) for token in self.command]

        if verbose:
            cmd_str = " ".join(substituted_command)
            print(f"=== substituted command: {cmd_str}")

        # mkdir when needed.
        if tempfile_transform.temp_dir:
            for new_arg in renamed_outputs.values():
                os.makedirs(os.path.dirname(new_arg))

        # Run the modified command.
        retval = subprocess.call(substituted_command)

        if retval != 0:
            # Option: clean-up .tmp files or leave them for inspection
            return retval

        # Otherwise command succeeded, so conditionally move outputs in-place.
        # TODO(fangism): This loop could be parallelized.
        for orig_out, temp_out in renamed_outputs.items():
            move_if_different(src=temp_out, dest=orig_out, verbose=verbose)

        if verbose:
            unrenamed_outputs = self.outputs - set(renamed_outputs.keys())
            if unrenamed_outputs:
                # Having un-renamed outputs is not an error, but rather an indicator
                # of a potentially missed opportunity to cache unchanged outputs.
                unrenamed_formatted = " ".join(unrenamed_outputs)
                print(f"  === Un-renamed outputs: {unrenamed_formatted}")

        return 0


def main_arg_parser() -> argparse.ArgumentParser:
    """Construct the argument parser, called by main()."""
    parser = argparse.ArgumentParser(
        description="Wraps a GN action to preserve unchanged outputs",
        argument_default=[],
    )
    # label is only used for diagnostics
    parser.add_argument("--label", help="The wrapped target's label")
    parser.add_argument(
        "--outputs", nargs="*", help="An action's declared outputs")
    parser.add_argument(
        "--depfile", help="A depfile that is written by the command")
    parser.add_argument(
        "--temp-suffix",
        type=str,
        default=".tmp",
        help="Suffix to use for temporary outputs",
    )
    parser.add_argument(
        "--temp-dir",
        type=str,
        default="",
        help=
        "Temporary directory for writing, can be relative to working directory or absolute.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Print information about which outputs were renamed/cached.",
    )
    parser.add_argument(
        "--disable",
        action="store_false",
        dest="enable",
        default=True,
        help="If disabled, run the original command as-is.",
    )

    # Positional args are the command and arguments to run.
    parser.add_argument("command", nargs="*", help="The command to run")
    return parser


def main():
    parser = main_arg_parser()
    args = parser.parse_args()

    tempfile_transform = TempFileTransform(
        temp_dir=args.temp_dir,
        suffix=args.temp_suffix,
    )
    if not tempfile_transform.valid:
        raise ValueError(
            "Need either --temp-dir or --temp-suffix, but both are missing.")

    wrap = args.enable
    # Decided whether or not to wrap the action script.
    ignored_scripts = {
        # If the action is only copying or linking, don't bother wrapping.
        "ln",
        "cp",  # TODO: Could conditionally copy if different.
        "rsync",
    }
    script = args.command[0]
    if os.path.basename(script) in ignored_scripts:
        wrap = False

    # If disabled, run the original command as-is.
    if not wrap:
        return subprocess.call(args.command)

    # Otherwise, rewrite the command using temporary outputs.
    outputs = set(args.outputs)
    if args.depfile:
        outputs.add(args.depfile)

    # Run a modified command that can leave unchanged outputs untouched.
    action = Action(
        command=args.command,
        outputs=outputs,
    )
    return action.run_cached(
        tempfile_transform=tempfile_transform, verbose=args.verbose)


if __name__ == "__main__":
    sys.exit(main())
