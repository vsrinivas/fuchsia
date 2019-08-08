#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re
import sys

begun = set()

for line in open(sys.argv[1]):
    m = re.search(
        r"(BEG|END) ([^ ]+) ([^ ]+).*packet_protocol:([^ \n]+)", line)
    if not m:
        continue
    tag = "%s:%s:%s" % (m.group(2), m.group(3), m.group(4))
    if m.group(1) == "BEG":
        assert tag not in begun
        begun.add(tag)
    else:
        assert tag in begun
        begun.remove(tag)

print begun
