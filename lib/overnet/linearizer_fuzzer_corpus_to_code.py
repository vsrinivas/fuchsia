#!/usr/bin/env python

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import hashlib


# Converts a corpus file to a unit test for linearizer_fuzzer


with open(sys.argv[1], 'rb') as f:
    inp = f.read()


def next_byte():
    global inp
    if not inp:
        return 0
    c = inp[0]
    inp = inp[1:]
    return ord(c)


def next_short():
    x = next_byte() << 8
    x |= next_byte()
    return x


def next_block(length):
    x = []
    for i in range(0, length):
        x.append(next_byte())
    return x


print 'TEST(LinearizerFuzzed, _%s) {' % hashlib.sha1(inp).hexdigest()
print '  linearizer_fuzzer::LinearizerFuzzer m;'
block_idx = 0
while True:
    op = next_byte()
    if op == 0:
        break
    elif op == 1:
        print '  m.Close(%d);' % next_byte()
    elif op == 2:
        print '  m.Pull();'
    else:
        offset = next_short()
        length = op - 2
        eom = length & 1
        length >>= 1
        blk = next_block(length)
        print '  static const uint8_t block%d[] = {%s};' % (
            block_idx, ','.join('0x%02x' % b for b in blk))
        print '  m.Push(%d, %d, %s, block%d);' % (
            offset, len(blk), 'true' if eom else 'false', block_idx)
        block_idx += 1
print '}'
