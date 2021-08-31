#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import os

_, output, include = sys.argv

header = f'''// Generated forwarding header
#include <{include}>
'''

if os.path.exists(output):
    if open(output, encoding='UTF8').read() == header:
        # header is already written
        sys.exit(0)

open(output, 'w', encoding='UTF8').write(header)