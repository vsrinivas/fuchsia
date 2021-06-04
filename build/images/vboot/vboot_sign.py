#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import subprocess
import sys

#####
#  Perform a ChromeOS style vboot signing
#
#  This converts a zbi into a "kernel partition" for ChromeOS targets.
#
#  Params:
#  -z  => path to the zbi
#  -o  => output path to write to
#  -B  => build working directory

VBOOT_DIR = "../../third_party/vboot_reference"
KERNEL_KEYBLOCK = VBOOT_DIR + "/tests/devkeys/kernel.keyblock"
PRIVATE_KEYBLOCK = VBOOT_DIR + "/tests/devkeys/kernel_data_key.vbprivk"


def main():
    parser = argparse.ArgumentParser(description='vboot sign a ZBI')
    parser.add_argument("-z", required=True, help="path the ZBI to sign.")
    parser.add_argument(
        "-o", required=True, help="path to write the signed kernel image to.")
    parser.add_argument(
        "-B", help="path to the build root, which all paths are relative to.")
    parser.add_argument("--host-tool", required=True)
    parser.add_argument("--kernel-keyblock", required=True)
    parser.add_argument("--private-keyblock", required=True)
    parser.add_argument("--multiboot", required=True)
    args = parser.parse_args()

    subprocess.run(
        [
            args.host_tool,
            "vbutil_kernel",
            "--pack",
            args.o,
            "--keyblock",
            args.kernel_keyblock,
            "--signprivate",
            args.private_keyblock,
            "--bootloader",
            args.z,
            "--vmlinuz",
            args.multiboot,
            "--version",
            "1",
            "--flags",
            "0x2",
        ],
        check=True)


if __name__ == '__main__':
    sys.exit(main())
