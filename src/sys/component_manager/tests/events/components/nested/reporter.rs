// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use component_events::{events::EventSource, matcher::EventMatcher, sequence::*};

#[fuchsia::component(logging_tags = ["nested_reporter"])]
async fn main() {
    // Track all the starting child components.
    let event_source = EventSource::new().unwrap();
    let event_stream = event_source.take_static_event_stream("StartedEventStream").await.unwrap();

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok().moniker_regex("./child_a"),
                EventMatcher::ok().moniker_regex("./child_b"),
                EventMatcher::ok().moniker_regex("./child_c"),
            ],
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}
