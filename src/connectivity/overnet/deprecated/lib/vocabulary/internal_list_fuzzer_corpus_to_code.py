#!/usr/bin/env python

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import hashlib


# Converts a corpus file to a unit test for internal_list_fuzzer


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


print 'TEST(InternalList, _%s) {' % hashlib.sha1(inp).hexdigest()
print '  internal_list::Fuzzer fuzzer;'
block_idx = 0
ops = {1: "PushBack", 2: "PushFront", 3: "Remove"}
while True:
    op_list = next_byte()
    op = op_list & 0x0f
    lst = op_list >> 4
    node = next_byte()
    if op in ops:
        print '  if (!fuzzer.%s(%d, %d)) return;' % (ops[op], node, lst)
    else:
        break
print '}'
