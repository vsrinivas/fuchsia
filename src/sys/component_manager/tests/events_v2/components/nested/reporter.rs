// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use component_events::{events::EventStream, matcher::EventMatcher, sequence::*};

#[fuchsia::main(logging_tags = ["nested_reporter"])]
async fn main() {
    // Track all the starting child components.
    let event_stream = EventStream::open().await.unwrap();

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok().moniker("./child_a"),
                EventMatcher::ok().moniker("./child_b"),
                EventMatcher::ok().moniker("./child_c"),
            ],
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}
