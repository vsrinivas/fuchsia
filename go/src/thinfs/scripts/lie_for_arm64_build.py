#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script which exists purely to satisfy the ARM64 build.
# It lies, and makes an empty thinfs file so gn doesn't blow up.
# TODO(smklein): Delete me when Go can build on ARM64.

from subprocess import call


def main():
    output = 'thinfs'
    return call(['touch', output])


if __name__ == '__main__':
    main()
