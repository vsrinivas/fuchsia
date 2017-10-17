#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import paths
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Generate Ninja files for Fuchsia",
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("--args", dest="gn_args", help="additional args to pass to gn",
                        action="append")
    parser.add_argument("--help-args", dest="gn_args_list", default=False,
                        help="Show GN build arguments usable in --args",
                        action="store_true")
    parser.add_argument("--zircon_project", "-z", help="zircon project",
                        default=os.environ.get("ZIRCON_PROJECT"))
    parser.add_argument("--packages", "-p", help="comma separated list of packages",
                        default="packages/gn/default")
    parser.add_argument("--debug", help="generate debug mode build files (default)",
                        dest="variant", default="debug", action="store_const", const="debug")
    parser.add_argument("--release", "-r", help="generate release mode build files",
                        dest="variant", action="store_const", const="release")
    parser.add_argument("--outdir", "-o", help="output directory")
    parser.add_argument("--target_cpu", "-t", help="Target CPU", default="x86-64",
                        choices=['x86-64', 'aarch64'])
    parser.add_argument("--goma", help="use goma", metavar="GOMADIR",
                        nargs='?', const=True, default=False)
    parser.add_argument("--ccache", "-c", help="use ccache",
                        action="store_true")
    parser.add_argument("--lto", nargs='?', const='thin', choices=['full', 'thin'],
                        default=None, help="use link time optimization (LTO)")
    parser.add_argument("--thinlto-cache-dir", help="ThinLTO cache directory")
    parser.add_argument("--ignore-skia", help="Disable Skia settings - for Skia-less builds",
                        action="store_true", default=False)
    parser.add_argument("--with-dart-analysis", help="Run Dart analysis as part of the build",
                        action="store_true", default=False)
    parser.add_argument("--autorun", help="path to autorun script")
    args = parser.parse_args()

    if not args.outdir:
        args.outdir = "out/%s" % args.variant

    # TODO: Do not clobber user specified output dir
    args.outdir += "-" + args.target_cpu

    outdir_path = os.path.join(paths.FUCHSIA_ROOT, args.outdir)

    if args.gn_args_list:
      gn_command = ["args", outdir_path, "--list"]
    else:
      gn_command = ["gen", outdir_path, "--check"]

    cpu_map = {"x86-64":"x64", "aarch64":"arm64"}
    gn_args = "--args=target_cpu=\"" + cpu_map[args.target_cpu]  + "\""

    if not args.ignore_skia:
        # Disable some Skia features not needed for host builds.
        # This is needed in order to build the Flutter shell.
        gn_args += " skia_enable_flutter_defines=true"
        gn_args += " skia_use_dng_sdk=false"
        gn_args += " skia_use_fontconfig=false"
        gn_args += " skia_use_libwebp=false"
        gn_args += " skia_use_sfntly=false"

    if args.with_dart_analysis:
        print("--with-dart-analysis is deprecated as analysis is now always on.")

    gn_args += " fuchsia_packages=\"" + args.packages + "\""

    if args.variant == "release":
        gn_args += " is_debug=false"
    if args.goma:
        gn_args += " use_goma=true"
        if type(args.goma) is str:
            path = os.path.abspath(args.goma)
            if not os.path.exists(path):
                parser.error('invalid goma path: %s' % path)
            gn_args += " goma_dir=\"" + path + "\""
    if args.ccache:
        gn_args += " use_ccache=true"
    if args.lto:
        gn_args += " use_lto = true"
        if args.lto == "full":
            gn_args += " use_thinlto = false"
        elif args.thinlto_cache_dir:
            gn_args += " thinlto_cache_dir=\"%s\"" % args.thinlto_cache_dir
    if args.autorun:
        abs_autorun = os.path.abspath(args.autorun)
        if not os.path.exists(abs_autorun):
            parser.error('invalid autorun path: %s' % args.autorun)
        gn_args += " autorun=\"%s\"" % abs_autorun
    if args.gn_args:
        gn_args += " " + " ".join(args.gn_args)

    if args.zircon_project:
        gn_args += " zircon_project=\"%s\"" % args.zircon_project

    return subprocess.call([paths.GN_PATH] + gn_command + [gn_args])


if __name__ == "__main__":
    sys.exit(main())
