#!/usr/bin/env python

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import statsc

statsc.compile(
    name='Link',
    include='src/connectivity/overnet/lib/stats/link.h',
    stats = [
        statsc.Counter(name='incoming_packet_count'),
        statsc.Counter(name='outgoing_packet_count'),
    ]
)
