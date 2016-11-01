#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to run Fuchsia in a variety of ways."""


import argparse
import os.path
import paths
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Run Fuchsia.")
    parser.add_argument("--arch", "-a", choices=["x86-64", "arm64"],
                        default="x86-64", help="architecture (default: x86-64)")
    parser.add_argument("--debug", "-d", dest="debug", action="store_true",
                        default=True, help="use debug build")
    parser.add_argument("--release", "-r", dest="debug", action="store_false",
                        help="use release build")
    parser.add_argument("--graphical-console", "-g", dest="graphical",
                        action="store_true", default=False,
                        help="use graphical console")
    parser.add_argument("--fuchsia-userspace", dest="fuchsia",
                        action="store_true", default=True,
                        help="use Fuchsia userspace")
    parser.add_argument("--magenta-userspace", dest="fuchsia",
                        action="store_false", help="use only Magenta userspace")
    parser.add_argument("--boot-server", dest="bootserver", action="store_true",
                        default=False,
                        help="run the bootserver instead of running via qemu")
    parser.add_argument("--verbose", dest="verbose", action="store_true",
                        default=False, help="you like spew")
    parser.add_argument("--dry-run", dest="dry_run", action="store_true",
                        default=False, help="just pretend to run stuff")

    args = parser.parse_args()

    fuchsia_build_dir = os.path.join(paths.FUCHSIA_ROOT, "out",
                                     ("debug" if args.debug else "release") +
                                         "-" + args.arch)
    # TODO(vtl): Not sure what to use instead of "qemu" for booting on real
    # ARM64 hardware (probably varies depending on hardware).
    magenta_build_dir = os.path.join(paths.FUCHSIA_ROOT, "out", "build-magenta",
            "build-magenta-%s-%s%s" %
                ("pc" if args.arch == "x86-64" else "qemu", args.arch,
                 "" if args.debug else "-release"))

    boot_fs = (os.path.join(fuchsia_build_dir, "user.bootfs") if args.fuchsia
                  else None)

    if args.bootserver:
        command = [os.path.join(paths.FUCHSIA_ROOT, "out", "build-magenta",
                   "tools", "bootserver")]
        command += [os.path.join(magenta_build_dir, "magenta.bin")]
        if boot_fs:
            command += [boot_fs]
    else:
        command = [os.path.join(paths.MAGENTA_ROOT, "scripts", "run-magenta")]
        command += ["-a", args.arch]
        command += ["-o", magenta_build_dir]
        if boot_fs:
            command += ["-x", boot_fs]
        if args.graphical:
            command += ["-g"]

    if args.verbose or args.dry_run:
        print "Running: " + " ".join(command)

    if args.dry_run:
        return 0

    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main())
