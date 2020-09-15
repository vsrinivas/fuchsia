#!/usr/bin/env python3.8

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import re
import sys


def substitute(replacements, input, output):
    for line in input.readlines():
        output.write(
            re.sub(r'\$\w+', lambda m: replacements.get(m.group(0)[1:]), line))


for file in sys.argv[2:]:
    input = open(sys.argv[1], 'r')
    output = open(file, 'w')
    substitute(
        {
            'binary':
                'test/' +
                os.path.splitext(os.path.basename(file))[0].replace('-', '_')
        }, input, output)
