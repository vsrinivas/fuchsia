// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::client::{EventId, TimedEvent, TimedEventClass},
    std::collections::HashMap,
    wlan_common::timer::TimeStream,
};

pub fn drain_timeouts(
    time_stream: &mut TimeStream<TimedEvent>,
) -> HashMap<TimedEventClass, Vec<(TimedEvent, EventId)>> {
    let mut timeouts = HashMap::new();
    loop {
        match time_stream.try_next() {
            Ok(Some((_, timed_event))) => {
                timeouts
                    .entry(timed_event.event.class())
                    .or_insert(vec![])
                    .push((timed_event.event, timed_event.id));
            }
            _ => return timeouts,
        };
    }
}
