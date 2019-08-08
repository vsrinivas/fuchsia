#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

sys.path.append(os.path.join(
    os.path.dirname(__file__),
    os.pardir,
    "images",
))
import elfinfo


def get_sdk_debug_path(binary):
    build_id = elfinfo.get_elf_info(binary).build_id
    return '.build-id/' + build_id[:2] + '/' + build_id[2:] + '.debug'


# For testing.
def main():
    print(get_sdk_debug_path(sys.argv[1]))


if __name__ == '__main__':
    sys.exit(main())
