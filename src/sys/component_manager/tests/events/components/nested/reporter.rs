// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use component_events::{
    events::{Event, EventMode, EventSource, EventSubscription, Started},
    matcher::EventMatcher,
    sequence::*,
};

#[fuchsia::component(logging_tags = ["nested_reporter"])]
async fn main() {
    // Track all the starting child components.
    let event_source = EventSource::new().unwrap();
    let event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Started::NAME], EventMode::Async)])
        .await
        .unwrap();
    event_source.start_component_tree().await;

    EventSequence::new()
        .all_of(
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
