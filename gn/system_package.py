#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import subprocess
import sys


def get_merkleroot(path, merkleroot):
    """
    Given a file at `path`, execute the host tool `merkleroot` in order to
    produce a merkle root hash of the path, and return it as a string.
    """
    return subprocess.check_output([merkleroot, path]).split()[0]


def main():
    """
    Produces a meta.far "package descriptor" using the pm host tool and a
    command line for zircon.system.blob-init that will mount the package
    at boot time.

    This script wraps the `pm` host tool for the build in order to create a
    Fuchsia "system package". The system package is an intermediate package
    that aids in the transition to fully individually packaged and sandboxed
    services in Fuchsia.

    outputs:
        * system-package-dir/meta.far - a signed package descriptor mountable
          by pkgsvr and pmd.
        * commandline - a file containing the zircon.system.blob-init and
          zircon.system.blob-init-arg lines sufficient to mount the
          produced meta.far system package using the supplied pkgsvr binary.
        * blob-manifest - a full manifest of all files that need to be
          added to blobfs (all system-manifest files, plus all
          packages in the pkgsvr-index).
    """

    parser = argparse.ArgumentParser("Generates a /system package")
    parser.add_argument("--system-manifest",
                        help="Typically system.manifest. All contained files will be included in the output package meta.far",
                        required=True)
    parser.add_argument("--system-package-dir",
                        help="Path to the system package directory. This directory will be recreated by this build and will contain the output meta.far",
                        required=True)
    parser.add_argument("--system-package-key",
                        help="Path to the system package key. Will be created if it does not exist.",
                        required=True)
    parser.add_argument("--pm",
                        help="Path to the pm host binary used to build and sign the package meta.far.",
                        required=True)
    parser.add_argument("--pkgsvr",
                        help="Path to the pkgsvr target binary that will host the system package at /system on target.",
                        required=True)
    parser.add_argument("--merkleroot",
                        help="Path to the host merkleroot binary from the Zircon tools build",
                        required=True)
    parser.add_argument("--commandline",
                        help="Path to the command line file to output, containing the /system mount arguments.",
                        required=True)
    parser.add_argument("--pkgsvr-index",
                        help="Path to the static packages index",
                        required=True)
    parser.add_argument("--blob-manifest",
                        help="Path to output the final blob manifest.",
                        required=True)
    args = parser.parse_args()

    if os.path.exists(args.system_package_dir):
        shutil.rmtree(args.system_package_dir)

    try:
        os.makedirs(args.system_package_dir)

        if not os.path.exists(args.system_package_key):
            print("NOTE: generating new system package key: %s\n",
                args.system_package_key)
            subprocess.check_call(
                [args.pm, "-k", args.system_package_key, "genkey"])

        # Create a meta/package as required for all packages.
        pkgjson_path = os.path.join(args.system_package_dir, "package")
        with open(pkgjson_path, "w") as pkgjson:
            pkgjson.write('{"name": "system.pkg", "version": "0"}')

        # Create a package manifest that includes a copy of all the contents
        # from args.system_manifest, plus meta/package and pkgsvr-index.
        package_manifest = os.path.join(args.system_package_dir, "package_manifest")
        with open(package_manifest, "w") as manifest:
            manifest.write("meta/package=%s\n" % os.path.relpath(pkgjson_path))
            manifest.write("data/static_packages=%s\n" % os.path.relpath(args.pkgsvr_index))
            with open(args.system_manifest, "r") as sys_manifest:
                for line in sys_manifest:
                    manifest.write(line)


        # Build produces meta/contents, meta/pubkey, meta/signature based on the
        # inputs, and then constructs the package meta.far.
        subprocess.check_call([args.pm, "-k", args.system_package_key, "-o",
                            args.system_package_dir, "-m", package_manifest, "build"])

        system_package_meta_far = os.path.join(args.system_package_dir, "meta.far")
        pkgsvr_merkle = get_merkleroot(args.pkgsvr, args.merkleroot)
        meta_far_merkle = get_merkleroot(system_package_meta_far, args.merkleroot)

        # args.commandline will contain the boot arguments required to cause pkgsvr
        # to mount the package defined by the `system-manifest` that is described by
        # `system_package_meta_far`. See
        # https://fuchsia.googlesource.com/zircon/+/master/docs/kernel_cmdline.md#zircon_system_blob_init_command
        with open(args.commandline, 'w') as cmdline:
            cmdline.write(
                "zircon.system.blob-init=/blob/%s\n" % pkgsvr_merkle)
            cmdline.write(
                "zircon.system.blob-init-arg=%s\n" % meta_far_merkle)

        with open(args.blob_manifest, 'w') as blob_manifest:
            blob_manifest.write("=%s\n" % os.path.relpath(system_package_meta_far))
            with open(package_manifest) as manifest:
                for line in manifest:
                    blob_manifest.write(line)

    except:
        if os.path.exists(args.system_package_dir):
            shutil.rmtree(args.system_package_dir)
        if os.path.exists(args.commandline):
            os.remove(args.commandline)
        raise

    return 0


if __name__ == '__main__':
    sys.exit(main())
