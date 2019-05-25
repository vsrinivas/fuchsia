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

        statsc.Counter(name='unseen_packets_marked_not_received'),
        statsc.Counter(name='acks_sent'),
        statsc.Counter(name='pure_acks_sent'),
        statsc.Counter(name='tail_loss_probes_scheduled'),
        statsc.Counter(name='tail_loss_probes_cancelled_because_requests_already_queued'),
        statsc.Counter(name='tail_loss_probes_cancelled_because_probe_already_scheduled'),
        statsc.Counter(name='tail_loss_probes_cancelled_after_timer_created'),

        statsc.Counter(name='tail_loss_probe_scheduled_because_ack_required_soon_timer_expired'),
        statsc.Counter(name='tail_loss_probe_scheduled_because_send_queue_is_empty'),

        statsc.Counter(name='ack_not_required_historic_sequence'),
        statsc.Counter(name='ack_not_required_frozen_sequence'),
        statsc.Counter(name='ack_not_required_invalid_packet'),
        statsc.Counter(name='ack_not_required_short_optional_run'),
        statsc.Counter(name='ack_required_soon_ack_received'),
        statsc.Counter(name='ack_required_soon_data_received'),
        statsc.Counter(name='ack_required_soon_continue_partial_after_ack'),
        statsc.Counter(name='ack_required_soon_all_acks_nacked'),
        statsc.Counter(name='ack_required_immediately_due_to_nack'),
        statsc.Counter(name='ack_required_immediately_due_to_partial_ack'),
        statsc.Counter(name='ack_required_immediately_due_to_multiple_receives'),
    ]
)
