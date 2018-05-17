#!/usr/bin/env python

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import hashlib


# Converts a corpus file to a unit test for packet_protocol_fuzzer


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


def next_block(length):
    x = []
    for i in range(0, length):
        x.append(next_byte())
    return x


print 'TEST(PacketProtocolFuzzed, _%s) {' % hashlib.sha1(inp).hexdigest()
print '  PacketProtocolFuzzer fuzzer;'
block_idx = 0
while True:
    op = next_byte()
    if op == 1:
        sender = next_byte()
        data_len = next_byte()
        data_pfx = next_byte()
        data = next_block(data_len)
        print '  static const uint8_t block%d[] = {%s};' % (
            block_idx, ','.join('0x%02x' % b for b in data))
        print '  if (!fuzzer.BeginSend(%d, Slice::WithInitializerAndPrefix(%d, %d, [](uint8_t* p) { memcpy(p, block%d, %d); }))) {return;}' % (
            sender, data_len, data_pfx, block_idx, data_len)
        block_idx += 1
    elif op == 2:
        sender = next_byte()
        send = next_64()
        status = next_byte()
        print '  if (!fuzzer.CompleteSend(%d, %dull, %d)) {return;}' % (
            sender, send, status)
    elif op == 3:
        print '  if (!fuzzer.StepTime(%dull)) {return;}' % next_64()
    else:
        break
print '}'
