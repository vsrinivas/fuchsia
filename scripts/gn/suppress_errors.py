#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Example usage:
$ fx set ...
$ scripts/gn/suppress_errors.py --issue=-Wshorten-64-to-32 --issue=-Wimplicit-int-float-conversion --config="//build/config:Wno-conversion"
"""

import argparse
import fileinput
import multiprocessing
import os
import re
import subprocess
import json
import sys
import functools
from typing import List, Dict


def run_command(command):
    return subprocess.check_output(
        command, stderr=subprocess.STDOUT, encoding="utf8")


# functools.cache is more semantically accurate, but requires Python 3.9
@functools.lru_cache
def get_out_dir():
    """Retrieve the build output directory"""

    fx_status = run_command(["fx", "status", "--format=json"])
    return json.loads(
        fx_status)["environmentInfo"]["items"]["build_dir"]["value"]


def get_common_gn_args():
    """Retrieve common GN args shared by several commands in this script.
    Returns:
      A list of GN command-line arguments (to be used after "gn <command>").
    """

    return [get_out_dir()]


def gn_find_refs(files: List[str], raise_if_no_match: bool = True) -> List[str]:
    """Wrapper around `fx gn refs`.

    Returns the targets referencing any of the files in `error_files`.
    """
    refs_command = ["fx", "gn", "refs"]
    refs_command += get_common_gn_args()
    refs_command += sorted(files)
    try:
        refs_out = run_command(refs_command)
    except subprocess.CalledProcessError as e:
        if ("The input matches no targets, configs, or files." in e.output and
                not raise_if_no_match):
            return []
        print(f"Failed to resolve references for {files}!", file=sys.stderr)
        print(e.output, file=sys.stderr)
        raise e
    return refs_out.splitlines()


def gn_find_refs_complete(
        path: str, sources_that_include_header: Dict[str,
                                                     List[str]]) -> List[str]:
    """Returns the targets referencing a path.
    If no targets refer to that path and the path is a header,
    fallback to looking for the targets that reference the source files which
    include that header.
    If still no targets, return an empty list.

    This function is module-level for reasons to do with how Python
    multiprocessing works.

    Args:
      params: tuple of (path, dictionary from header to list of sources)
    """

    refs = gn_find_refs([path], raise_if_no_match=False)
    if refs:
        return refs

    # If no direct reference, look for refs for the sources instead.
    print(
        f"Note: looking for source files which includes {path}, "
        "because the path was not tracked by the build system",
        file=sys.stderr)
    sources = sources_that_include_header.get(path, [])
    if not sources:
        print(
            f"Warning: {path} was not tracked by the build system, "
            "and not included by any sources",
            file=sys.stderr)
        return []
    return gn_find_refs(sources)


def can_have_config(params):
    """Returns whether the given target can have a config.
    If not sure, returns True.

    This function is module-level for reasons to do with how Python
    multiprocessing works.

    Args:
      params: tuple of target to examine, config to add.
              Packed in a single tuple for reasons to do with how Python
              multiprocessing works.
    """

    (
        target,
        config,
    ) = params

    desc_command = ["fx", "gn", "desc"]
    desc_command += get_common_gn_args()

    try:
        desc_out = run_command(desc_command + [target, "configs"])
        # Target has configs and they include the given config.
        # Can't add a duplicate config!
        return config not in desc_out
    except subprocess.CalledProcessError as e:
        if 'Don\'t know how to display "configs" for ' in e.output:
            # Target type cannot have configs,
            # or the actual target is in another toolchain.
            # Assume the latter.
            return True
        elif " matches no targets, configs or files" in e.output:
            # The target probably exists in a non-default toolchain.
            # Assume that it can have the config.
            return True
        else:
            raise e


included_from_regex = re.compile("In file included from (.*?\.(c|cc|cpp)):\d*")


def find_cc_file_using_included_from(prev_lines: str):
    """Given the last lines of compiler output, find the first path that
    is not of the 'In file included from ...' format, which is probably the
    source file that led to the error by including a chain of headers.
    """
    # The last line should be a header file reference
    assert "In file included from" not in prev_lines[
        -1], f"unexpected {prev_lines}"
    # Go backwards until we hit a '.cc' or '.cpp' file
    for l in reversed(prev_lines[:-1]):
        match = included_from_regex.match(l)
        if match:
            return os.path.normpath(os.path.join(get_out_dir(), match.group(1)))
    raise RuntimeError(f"Cannot find .cc/.cpp file in {prev_lines}")


def main():
    parser = argparse.ArgumentParser(
        description="Adds a given config to all compile targets with "
        "a given compiler error")
    parser.add_argument(
        "--fx-build-log",
        help="Captured output from a build invocation. "
        "If not provided, the tool will invoke the build first.",
    )
    parser.add_argument(
        "--confirm",
        help="If true, pause after each step in the script to review changes",
        action="store_true",
        default=False,
    )
    parser.add_argument(
        "--complete",
        help=
        "If true, attempt to produce a complete annotation by attributing errors "
        "in unreferenced headers to source files that include that header.",
        action="store_true",
        default=False,
    )
    parser.add_argument(
        "--issue",
        action='append',
        required=True,
        help="A regex matching the intended compiler error/warning. "
        "E.g. --issue='error:.*-Wconversion'."
        "Take care to avoid character expansion by the shell, for example "
        "by using single quotation marks.")
    parser.add_argument("--config", required=True, help="GN config to add")
    parser.add_argument("--comment", help="Comment to add before config")
    args = parser.parse_args()
    fx_build_log = args.fx_build_log
    confirm = args.confirm
    complete = args.complete
    issue = '|'.join(args.issue)
    config = args.config
    comment = args.comment

    log_regex = re.compile(f"(?:\.\./)*([^:]*):\d*:\d*:.*({issue})")

    if fx_build_log:
        with open(fx_build_log, "r") as f:
            build_out = f.readlines()
    else:
        # Harvest all compilation errors
        print("Building...")
        try:
            # On error continue building to discover all failures
            build_out = run_command(["fx", "build", "-k0"])
        except subprocess.CalledProcessError as e:
            build_out = e.output
        build_out = build_out.splitlines()

    sources_that_include_header = {}
    error_files = set()
    prev_lines = []
    for line in build_out:
        prev_lines.append(line)
        # We probably won't have more than 30 layers of header inclusions.
        prev_lines = prev_lines[-30:]
        match = log_regex.match(line)
        if match:
            path = os.path.normpath(match.group(1))
            if path.endswith(".h"):
                # Trace back to a '.cc' file, and record on the side
                sources = sources_that_include_header.get(path, [])
                sources.append(find_cc_file_using_included_from(prev_lines))
                sources_that_include_header[path] = sources
            error_files.add(path)
    print("Sources with compilation issues:")
    print("\n".join(sorted(error_files)))
    print()
    if not error_files:
        return 0
    if confirm:
        input("Press Enter to continue...")

    # Collect all BUILD.gn files with targets referencing failing sources
    print("Resolving references...")
    target_no_toolchain = re.compile("(\/\/[^:]*:[^(]*)(?:\(.*\))?")
    if complete:
        # Run `gn ref` on every error file individually. This is the only way
        # we can determine if a certain file does not have a reference.
        # Cap the max parallelism as the `gn refs` operation turned out very
        # memory intensive.
        with multiprocessing.Pool(min(multiprocessing.cpu_count(), 20)) as p:
            refs_lists = p.starmap(
                gn_find_refs_complete,
                ((path, sources_that_include_header) for path in error_files),
            )
            error_targets = {
                target_no_toolchain.match(ref).group(1)
                for refs in refs_lists
                for ref in refs
            }
    else:
        # Use a single `gn refs` invocation to find all references.
        # Note that this may silently omit files without references.
        refs_out = gn_find_refs(error_files)
        error_targets = {
            target_no_toolchain.match(ref).group(1) for ref in refs_out
        }
    print("Targets with the error:")
    print(error_targets)
    print()
    if confirm:
        input("Press Enter to continue...")

    # Remove targets that already have the given config
    # or can't have a config in the first place
    print("Removing irrelevant targets...")
    # `gn desc` is slow so parallelize
    # cap the max parallelism as the `gn refs` operation turned out very
    # memory intensive.
    with multiprocessing.Pool(min(multiprocessing.cpu_count(), 20)) as p:
        target_can_have = zip(
            error_targets,
            p.map(
                can_have_config,
                ((target, config) for target in error_targets)),
        )
        error_targets = {
            target for target, can_have in target_can_have if can_have
        }

    print("Failing targets:")
    print("\n".join(sorted(error_targets)))
    print()
    if confirm:
        input("Press Enter to continue...")

    # Fix failing targets
    ref_regex = re.compile("//([^:]*):([^.(]*).*")
    for target in sorted(error_targets):
        match = ref_regex.match(target)
        build_dir, target_name = match.groups()
        target_regex = re.compile(
            '^\s*\w*\("' + re.escape(target_name) + '"\) {')
        build_file = os.path.join(build_dir, "BUILD.gn")
        # Format file before processing
        run_command(["fx", "format-code", "--files=" + build_file])
        print("Fixing", target)
        in_target = False
        config_printed = False
        in_configs = False
        curly_brace_depth = 0
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
                if comment:
                    print("#", comment)
                print('configs += [ "' + config + '"]')
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
                if comment:
                    print("#", comment)
                print('"' + config + '",')
                in_configs = False
                config_printed = True
            print(line, end="")
            if config_printed and not in_target:
                # Reset for a possible redefinition of the same target
                # (e.g. within another conditional block)
                config_printed = False
        run_command(["fx", "format-code", "--files=" + build_file])

    print("Fixed all of:")
    for error_target in sorted(error_targets):
        print('  "' + error_target + '",')

    return 0


if __name__ == "__main__":
    sys.exit(main())
