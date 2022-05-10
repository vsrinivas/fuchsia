#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess

def compile_asl(path: str) -> bool:
    print('{}:'.format(os.path.basename(path)))
    with os.scandir(path) as sd:
        for file in sd:
            if not file.is_file():
                continue
            if not file.name.endswith('.asl'):
                continue

            cmd = ['iasl', file.name]
            print('    `{}`... '.format(' '.join(cmd)), end='')
            result = subprocess.run(cmd, cwd=path, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode != 0:
                print('FAILED. Logs from run:')
                print('====================================')
                print(result.stdout)
                print('====================================')
                return False
            print('OK')
    return True

with os.scandir('.') as sd:
    for file in sd:
        if not file.is_dir():
                continue
        if not compile_asl(file.path):
            break

