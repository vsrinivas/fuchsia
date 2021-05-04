#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys


def do_remap(build_id):
    """
    >>> do_remap('1815548680a59ffa')
    '86541518A580FA9F0000000000000000'
    """
    build_id = build_id.upper()
    # See https://crashpad.chromium.org/bug/229 and
    # https://source.chromium.org/chromium/chromium/src/+/main:third_party/crashpad/crashpad/snapshot/elf/module_snapshot_elf.cc;l=157-167;drc=81cc8267d3a069163708f3ac140d0d940487c137
    return (
        build_id[6:8]
        + build_id[4:6]
        + build_id[2:4]
        + build_id[0:2]
        + build_id[10:12]
        + build_id[8:10]
        + build_id[14:16]
        + build_id[12:14]
        + +(32 - len(build_id)) * "0"
    )


def main():
    if len(sys.argv) != 2:
        print("expected a single [normally 16 character] build id.", file=sys.stderr)
        return 1
    build_id = sys.argv[1].upper()

    # debug id is normally a PDB's guid + an "age". For historical reasons, the build id gets
    # swizzled (in the remap function) but is still 32 characters. Add the trailing 0 for a pseudo
    # age that the crash server assumes.
    age = "0"
    print(do_remap(sys.argv[1]) + age)


if __name__ == "__main__":
    sys.exit(main())
