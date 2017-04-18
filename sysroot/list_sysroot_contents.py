#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import os

def main():
    sysroot = sys.argv[1]
    inputs = []
    outputs = []
    for source_sysroot in sys.argv[2:]:
        for entry in os.walk(source_sysroot):
            dirpath, dirnames, filenames = entry
            for filename in filenames:
                inputs.append(os.path.join(dirpath, filename))
                relative_path = os.path.relpath(os.path.join(dirpath, filename),
                    source_sysroot)
                outputs.append('//' + os.path.join(sysroot, relative_path))

    print 'inputs = ["%s"]\n' % ('", "'.join(inputs))
    print 'outputs = ["%s"]\n' % ('", "'.join(outputs))

if __name__ == '__main__':
    sys.exit(main())
