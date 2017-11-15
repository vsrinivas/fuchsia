#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys


def efi_mkdir(image, path):
    """
    Create a directory at `path` inside the msdos partition image at `image`.
    """
    subprocess.check_call(["mmd", "-i", image, "::" + path],
                          stdout=open(os.devnull, 'w'))


def efi_cp(image, src, dst):
    """
    Copy a file from `src` into `dst` inside the msdos image at `image`.
    """
    subprocess.check_call(
        ["mcopy", "-i", image, src, "::" + dst], stdout=open(os.devnull, 'w'))


def main():
    """
    Produces an EFI partition containing the given kernel, ramdisk and
    bootloader.

    Note: this script relies on GNU mtools, which must be installed in the
    build environment and available on $PATH.
    """

    parser = argparse.ArgumentParser("Make bootable EFI partition")
    parser.add_argument("--efi-bootloader",
                        help="An EFI bootloader to install, typically bootx64.efi",
                        required=False)
    parser.add_argument("--zircon",
                        help="A bootable kernel image to install",
                        required=True)
    parser.add_argument("--bootdata",
                        help="A bootdata to include in the image",
                        required=False)
    parser.add_argument("--mkfs-msdosfs",
                        help="The zircon host tool mkfs-msdosfs for making FAT images",
                        required=True)
    parser.add_argument("--output",
                        help="The target image path to write to",
                        required=True)
    parser.add_argument("--output-size",
                        help="The size of the target partition image.",
                        default=63 * 1024 * 1024,
                        type=int,
                        required=False)

    args = parser.parse_args()

    if os.path.exists(args.output):
        os.remove(args.output)

    try:
        with open(args.output, "w") as image:
            image.truncate(args.output_size)

        subprocess.check_call([args.mkfs_msdosfs,
                               "-L", "ESP",
                               "-O", "Fuchsia",
                               "-F", "32",
                               "-b", "512",
                               "-S", str(args.output_size),
                               args.output],
                              stdout=open(os.devnull, 'w'),
                              stderr=subprocess.STDOUT)

        # TODO(TO-602): replace mtools dependency with a thinfs host tool.
        efi_cp(args.output, args.zircon, "zircon.bin")
        if args.bootdata:
            efi_cp(args.output, args.bootdata, "ramdisk.bin")
        if args.efi_bootloader:
            efi_mkdir(args.output, "EFI")
            efi_mkdir(args.output, "EFI/BOOT")
            efi_cp(args.output, args.efi_bootloader, "EFI/BOOT/BOOTX64.EFI")
    except:
        if os.path.exists(args.output):
            os.remove(args.output)
        raise

    return 0


if __name__ == '__main__':
    sys.exit(main())
