#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

def main(cmdline_file, merkleroots_file):
    with open(merkleroots_file) as input:
        [pkgsvr, meta_far] = [line.split(None, 1)[0]
                              for line in input.readlines()]
        with open(cmdline_file, 'w') as out:
            out.write(('zircon.system.blob-init=/blob/%s\n' % pkgsvr) +
                      ('zircon.system.blob-init-arg=%s\n' % meta_far))

if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))
