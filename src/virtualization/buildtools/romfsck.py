#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

def main():
  with open(sys.argv[1], 'rb') as image:
    with open(sys.argv[2], 'w') as output:
      image.seek(0, 2)
      if (image.tell() % 1024):
        print('File not block aligned')
        exit(-1)
      image.seek(0)
      magic = image.read(8)
      if (magic != b'-rom1fs-'):
        print('Bad header magic {}'.format(magic))
        exit(-1)
      image.seek(0)
      csum = 0
      i = 3
      for b in image.read(512):
        csum += (b if isinstance(b, int) else ord(b)) << (8 * i)
        if (i > 0):
          i -= 1
        else:
          i = 3
      csum &= 0xFFFFFFFF
      if (csum != 0):
        print('Bad checksum {}'.format(csum))
        exit(-1)
      output.write('ok')

if __name__ == '__main__':
  sys.exit(main())
