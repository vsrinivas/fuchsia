#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# manifest_list_collect_unique_blobs.py list-input-file manifest-output-file
#
# Given a file containing a list of manifest files, each of which contain dest
# values (left side of =) that is a content-address, sort the content address
# and only emit a single line for a given content address. The paths on the
# right hand side will be rebased relative to the directory containing the
# manifest output file.

import os
import sys

infile = sys.argv[1]
outfile = sys.argv[2]

outdir = os.path.dirname(outfile)

files = {}
with open(infile) as listfile:
  for manifest in listfile:
    with open(manifest.strip()) as f:
      for line in f:
        try:
          line = line.strip()
          if line == "":
            continue
          (id, src) = line.split("=", 2)
          # There are often cases where one content address has more than one
          # possible source, that's fine, it does not matter which we use.
          files[id] = src
        except:
          sys.stderr.write("Failed on line: " + line)
          raise

with open(outfile, "w") as f:
  for id, src in files.items():
    f.write(id + "=")
    f.write(os.path.relpath(src, outdir))
    f.write("\n")
