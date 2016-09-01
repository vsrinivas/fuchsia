#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import string
import sys


def main():
  label = sys.argv[1]
  if not label.startswith("//"):
      print "expected label to start with //, got %s" % label
      return 1
  # TODO: get_label_info(target_name, "dir") for //foo seems to return
  # //bar/foo/foo, so strip the last component.
  print ".".join(label[2:].split("/")[:-1])
  return 0

if __name__ == '__main__':
  sys.exit(main())
