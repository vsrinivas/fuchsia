#!/usr/bin/env python
#
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

def remove_prefix(text, prefix):
    return text[text.startswith(prefix) and len(prefix):]

# convert a directory corresponding to a GN-style label (ex garnet/foo/services/services)
# to a crate name (ex garnet_foo_services).
def label_to_crate(label):
    parts = label.split('/')
    if parts[-1] == parts[-2]:
        parts.pop()
    return '_'.join(parts)


def main():
  print label_to_crate(remove_prefix(sys.argv[1], "//"))
  return 0


if __name__ == '__main__':
    sys.exit(main())
