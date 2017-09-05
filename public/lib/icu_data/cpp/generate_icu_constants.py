#!/usr/bin/env python
# Copyright 2015 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import hashlib

in_file = sys.argv[1]
out_file = sys.argv[2]

out_dir = os.path.dirname(out_file)

data = None
with open(in_file, "rb") as f:
    data = f.read()

if not os.path.exists(out_dir):
    os.makedirs(out_dir)

sha1hash = hashlib.sha1(data).hexdigest()

with open(out_file, "w") as f:
    f.write('#include "apps/icu_data/lib/constants.h"\n')
    f.write("namespace icu_data {\n")
    f.write("const uint64_t kDataSize = %s;\n" % len(data))
    f.write("const char kDataHash[] = \"%s\";\n" % sha1hash)
    f.write("\n}  // namespace icu_data\n")
