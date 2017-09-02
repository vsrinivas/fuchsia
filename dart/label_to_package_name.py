#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import string
import sys

_LAYER_PREFIX = [
  "garnet/public/",
  "peridot/public/",
]


# Strip the layer prefix from the given label, if necessary.
def _remove_layer(label):
  for prefix in _LAYER_PREFIX:
    if label.startswith(prefix):
      return label[len(prefix):]
  return label


# For target //foo/bar:blah, the package name will be foo.bar..blah.
# For default targets //foo/bar:bar, the package name will be foo.bar.
def convert(label):
  if not label.startswith("//"):
      sys.stderr.write("expected label to start with //, got %s\n" % label)
      return 1
  base = _remove_layer(label[2:])
  separator_index = string.rfind(base, ":")
  if separator_index < 0:
      sys.stderr.write("could not find target name in label %s\n" % label)
      return 1
  path = base[:separator_index].split("/")
  name = base[separator_index+1:]
  if path[-1] == name:
      return ".".join(path)
  else:
      return "%s..%s" % (".".join(path), name)


def main():
  print convert(sys.argv[1])


if __name__ == '__main__':
  sys.exit(main())
