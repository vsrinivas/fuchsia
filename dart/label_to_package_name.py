#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import string
import sys


# For target //foo/bar:blah, the package name will be foo.bar..blah.
# For default targets //foo/bar:bar, the package name will be foo.bar.
def main():
  label = sys.argv[1]
  if not label.startswith("//"):
      print "expected label to start with //, got %s" % label
      return 1
  base = label[2:]
  separator_index = string.rfind(base, ":")
  if separator_index < 0:
      print "could not find target name in label %s" % label
      return 1
  path = base[:separator_index].split("/")
  name = base[separator_index+1:]
  if path[-1] == name:
      print ".".join(path)
  else:
      print "%s..%s" % (".".join(path), name)
  return 0

if __name__ == '__main__':
  sys.exit(main())
