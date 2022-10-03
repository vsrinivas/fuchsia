# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities to process clang output."""

def process_clang_builtins_output(probe_output):
    """Get Clang builtin data

    Args:
      probe_output: The stderr output of running `clang -x c++ -E -v ./empty`
    Returns:
      A tuple of:
        - The clang short version, as a string.
        - The clang long version, as a string.
        - A list of builtin include paths, as a list of path strings.
    """
    short_version = None
    long_version = None
    builtin_include_paths = []

    has_include_paths = False
    has_version = False
    clang_version_prefix = "clang version "
    for line in probe_output.splitlines():
        if not has_version:
            # Example inputs:
            # Fuchsia clang version 15.0.0 (https://llvm.googlesource.com/a/llvm-project 3a20597776a5d2920e511d81653b4d2b6ca0c855)
            # Debian clang version 14.0.6-2
            pos = line.find(clang_version_prefix)
            if pos >= 0:
                long_version = line[pos + len(clang_version_prefix):].strip()

                # Remove space followed by opening parenthesis.
                pos = long_version.find("(")
                if pos >= 0:
                    long_version = long_version[:pos].rstrip()

                # Remove -suffix
                pos = long_version.find("-")
                if pos >= 0:
                    long_version = long_version[:pos]

                # Split at dots to get the short version.
                short_version, _, _ = long_version.partition(".")
                has_version = True
        if not has_include_paths:
            if line == "#include <...> search starts here:":
                has_include_paths = True
        elif line.startswith(" /"):
            if line.startswith((" /usr/include", " /usr/local/include")):
                # ignore lines like /usr/include which should not be used by
                # our build system. Note that some users have their home
                # directory under /usr/local/something/.... and such paths
                # should not be filtered out.
                # See https://fxbug.dev/110637 for details.
                continue
            builtin_include_paths.append(line.strip())

    return (short_version, long_version, builtin_include_paths)
