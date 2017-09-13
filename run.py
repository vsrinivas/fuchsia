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
    parser.add_argument("--zircon-userspace", dest="fuchsia",
                        action="store_false", help="use only Zircon userspace")
    parser.add_argument("--boot-server", dest="bootserver", action="store_true",
                        default=False,
                        help="run the bootserver instead of running via qemu")
    parser.add_argument("--kernel-cmdline", "-c", dest="kernel_cmdline",
                        help="kernel command line")
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
    zircon_build_dir = os.path.join(paths.FUCHSIA_ROOT, "out", "build-zircon",
            "build-zircon-%s-%s" % ("pc" if args.arch == "x86-64" else "qemu", args.arch))

    qemu_dir = os.path.join(paths.FUCHSIA_ROOT, "buildtools", "qemu", "bin")

    boot_fs = (os.path.join(fuchsia_build_dir, "user.bootfs") if args.fuchsia
                  else None)

    if args.bootserver:
        command = [os.path.join(paths.FUCHSIA_ROOT, "out", "build-zircon",
                   "tools", "bootserver")]
        command += [os.path.join(zircon_build_dir, "zircon.bin")]
        if boot_fs:
            command += [boot_fs]
    else:
        command = [os.path.join(paths.ZIRCON_ROOT, "scripts", "run-zircon")]
        command += ["-a", args.arch]
        command += ["-o", zircon_build_dir]
        command += ["-q", qemu_dir]
        if boot_fs:
            command += ["-x", boot_fs]
        if args.graphical:
            command += ["-g"]
        if args.kernel_cmdline:
            command += ["-c", args.kernel_cmdline]

    if args.verbose or args.dry_run:
        print "Running: " + " ".join(command)

    if args.dry_run:
        return 0

    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main())
