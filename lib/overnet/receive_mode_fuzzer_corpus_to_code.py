#!/usr/bin/env python

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import hashlib

with open(sys.argv[1], 'rb') as f:
    inp = f.read()


def next_byte():
    global inp
    if not inp:
        return 0
    c = inp[0]
    inp = inp[1:]
    return ord(c)


def next_64():
    val = 0
    shift = 0
    while True:
        b = next_byte()
        val |= (b & 0x7f) << shift
        shift += 7
        if b & 0x80 == 0:
            break
    if val > (1 << 64) - 1:
        return 0
    return val


print 'TEST(ReceiveModeFuzzed, _%s) {' % hashlib.sha1(inp).hexdigest()
print ('  receive_mode::Fuzzer m(%d);'
       % next_byte())
while True:
    print '  m.Step();'
    op = next_byte()
    if op == 1:
        print ('  if (!m.Begin(%dull)) return;'
               % next_64())
    elif op == 2:
        print '  if (!m.Completed(%dull, %d)) return;' % (
            next_64(), next_byte())
    else:
        break
print '}'
