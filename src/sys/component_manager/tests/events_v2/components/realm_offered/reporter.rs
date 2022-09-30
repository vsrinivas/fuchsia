// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::EventStream, matcher::EventMatcher, sequence::*},
    fidl_fidl_test_components as ftest,
    fuchsia_component::client::connect_to_protocol,
};

#[fuchsia::main]
async fn main() {
    // Track all the starting components.
    let event_stream = EventStream::open().await.unwrap();
    // Connect to the parent offered Trigger. The parent will start the lazy child components and
    // this component should know about their started events given that it was offered those
    // events.
    let trigger =
        connect_to_protocol::<ftest::TriggerMarker>().expect("error connecting to trigger");
    trigger.run().await.expect("start trigger failed");
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
