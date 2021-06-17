#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Perform a ChromeOS-style vboot signing.

This converts a ZBI into a "kernel partition" for ChromeOS targets.
See vboot.gni, which supplies most of the arguments used here.

The -z switch for the input ZBI and the -o switch for the output image
are a fixed protocol with the assembly tools.  The rest of the arguments
are just piped through from the settings found in vboot.gni:vboot_action.

This wrapper script only needs to exist because the assembly tools always use
-z and -o for the input and output files.  The assembly configuration adds
arbitrary additional arguments to the script, but those two switches are always
passed implicitly.  If those tools were changed to accept switches to say what
switches to use for those, vboot.gni could give sufficient details for the
//build/images/assembly/BUILD.gn code to parameterize assembly configuration to
run the `futility` tool directly without Python.

"""

import argparse
import shlex
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Sign a ZBI as a vboot image")
    parser.add_argument("--zbi", "-z", required=True, metavar="FILE.zbi",
                        help="path the ZBI to sign.")
    parser.add_argument("--output", "-o", required=True, metavar="FILE",
                        help="path to write the signed kernel image to.")
    parser.add_argument("--tool", required=True, metavar="FILE",
                        help="Path to signing tool (futility)")
    parser.add_argument("--args", required=True, action="append",
                        metavar="ARG", help="Initial arguments to the tool")
    parser.add_argument("--input-args", required=True, action="append",
                        metavar="ARG", help="Arguments preceding input file")
    parser.add_argument("--output-args", required=True, action="append",
                        metavar="ARG", help="Arguments preceding output file")
    parser.add_argument("--verbose", action="store_true",
                        help="Display the command being run")
    parser.add_argument("--quiet", dest="verbose", action="store_false",
                        help="Don't display the command being run")
    args = parser.parse_args()

    cmd = ([ args.tool ] + args.args +
           args.output_args + [ args.output ] +
           args.input_args + [ args.zbi ])

    if args.verbose:
        sys.stderr.write("+ {cmd}\n".format(
            cmd=" ".join([shlex.quote(arg) for arg in cmd])))

    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    sys.exit(main())
