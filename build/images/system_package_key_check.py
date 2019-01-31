#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import sys

def main(pm_binary, stamp_file, key_file):
    if not os.path.exists(key_file):
        subprocess.check_call([pm_binary, '-k', key_file, 'genkey'])
    with open(stamp_file, 'w') as f:
        f.write('OK\n')
    return 0

if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))
