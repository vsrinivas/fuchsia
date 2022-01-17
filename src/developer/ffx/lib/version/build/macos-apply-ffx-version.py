#!/usr/bin/env python3
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(fxbug.dev/91330): Once bug is fixed, this hack can be removed.

# Create a copy of the inputs given by RESPONSE_PATH (argv[1]) at OUTPUT
# (argv[2]) with the content matching MATCH1 (argv[3]) replaced by the contents
# of the file given in REPLACEMENT1 (argv[4]) and content matching MATCH2
# (argv[5]) replaced with the contents of the file given in REPLACEMENT2
# (argv[6]).

import os
import stat
import sys

RESPONSE_PATH = sys.argv[1]
OUTPUT_PATH = sys.argv[2]

MATCH1 = sys.argv[3]
REPLACEMENT_PATH1 = sys.argv[4]

MATCH2 = sys.argv[5]
REPLACEMENT_PATH2 = sys.argv[6]

input = b""

with open(RESPONSE_PATH) as r:
    for input_path in r.readlines():
        with open(input_path.strip(), 'rb') as f:
            input += f.read()

with open(REPLACEMENT_PATH1, 'rb') as f:
    replacement1 = f.read()

with open(REPLACEMENT_PATH2, 'rb') as f:
    replacement2 = f.read()

output = input.replace(bytes(MATCH1, 'utf-8'), bytes(replacement1))
output = output.replace(bytes(MATCH2, 'utf-8'), bytes(replacement2))

if os.path.exists(OUTPUT_PATH):
    with open(OUTPUT_PATH, 'rb') as f:
        if f.read() == output:
            sys.exit(0)

with open(OUTPUT_PATH, 'wb') as f:
    f.truncate()
    f.write(output)

# chmod ug+x $OUTPUT_PATH
st = os.stat(OUTPUT_PATH)
os.chmod(OUTPUT_PATH, st.st_mode | stat.S_IXUSR | stat.S_IXGRP)
