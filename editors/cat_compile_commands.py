#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Logically concatenates given compile_commands.json files to stdout.

Filenames of compile_commands.json files are provided as arguments.  A logical
concatenation of all the compile commands is output to stdout.

Example:
    <layer>$ ./scripts/cat_compile_commands.py zircon/compile_commands.json \
    out/debug_x86-64/compile_commands.json > compile_commands.json

The output is suitable for use with vscode + cquery which needs a single
compile_commands.json for the whole editor workspace.  This way a single vscode
workspace can handle zircon + other layers of fuchsia, by adding the topaz
folder with the combined compile_commands.json in it.

Todo:
    * For zircon, in docs that mention "bear make -j20", see also this script.
    * For rest of fuchsia, integrate something like
      "~/topaz/buildtools/ninja -v -C \
      /usr/local/google/home/dustingreen/topaz/out/debug-x86-64 \
      -t compdb cc cxx objc objcxx x64-shared_cc x64-shared_cxx \
      > ~/topaz/out/debug-x86-64/compile_commands.json
      into the build - ideally only if build.ninja or maybe *.ninja change.
    * Iff the ~0.65s execution time becomes an issue, consider a more optimized
      raw binary splice based on fixing up the "]" and "[" and the end of first
      file and start of second file.
    * Document all the steps to get a nicely working vscode + cquery workspace
      with zircon + the rest of fuchsia in a combined editor workspace.
"""

import json
import sys


def main():
    """Cat compile_commands.json files given as args to stdout"""
    data = []
    for arg in sys.argv[1:]:
        data += json.load(open(arg))
    json.dump(data, sys.stdout, indent=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
