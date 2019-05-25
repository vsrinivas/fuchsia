#!/usr/bin/env python

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import statsc

statsc.compile(
    name='Stream',
    include='src/connectivity/overnet/lib/stats/stream.h',
    stats = [
        statsc.Counter(name='linearizer_reject_past_end_of_buffering'),
        statsc.Counter(name='linearizer_empty_chunk'),
        statsc.Counter(name='linearizer_fast_path_taken'),
        statsc.Counter(name='linearizer_ignore_all_prior'),
        statsc.Counter(name='linearizer_partial_ignore_begin'),
        statsc.Counter(name='linearizer_new_pending_queue'),
        statsc.Counter(name='linearizer_integrations'),
        statsc.Counter(name='linearizer_integration_inserts'),
        statsc.Counter(name='linearizer_integration_errors'),
        statsc.Counter(name='linearizer_integration_coincident_shorter'),
        statsc.Counter(name='linearizer_integration_coincident_longer'),
        statsc.Counter(name='linearizer_integration_prior_longer'),
        statsc.Counter(name='linearizer_integration_prior_partial'),
        statsc.Counter(name='linearizer_integration_subsequent_splits'),
        statsc.Counter(name='linearizer_integration_subsequent_covers'),
        statsc.Counter(name='send_chunk_cancel_packet_too_small'),
        statsc.Counter(name='send_chunk_split_packet_too_small'),
        statsc.Counter(name='send_chunk_take_entire_chunk'),
        statsc.Counter(name='send_chunk_nacked'),
        statsc.Counter(name='send_chunk_push'),
    ]
)
