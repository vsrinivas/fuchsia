// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{Destroyed, Event, EventStream, Started},
        matcher::EventMatcher,
    },
    fuchsia_component_test::ScopedInstance,
};

#[fuchsia::main(logging_tags = ["async_reporter"])]
async fn main() {
    let _ = EventStream::open().await.unwrap();
    // Validate that subscribing a second time works.
    let mut event_stream = EventStream::open().await.unwrap();
    let mut instances = vec![];
    let url = "#meta/stub_component_v2.cm".to_string();
    for _ in 1..=3 {
        let scoped_instance = ScopedInstance::new("coll".to_string(), url.clone()).await.unwrap();
        let _ = scoped_instance.connect_to_binder().unwrap();
        instances.push(scoped_instance);
    }

    for _ in 1..=3 {
        let event = EventMatcher::ok().expect_match::<Started>(&mut event_stream).await;
        assert_eq!(event.component_url(), url);
    }

    // Dropping instances destroys the children.
    drop(instances);

    for _ in 1..=3 {
        let event = EventMatcher::ok().expect_match::<Destroyed>(&mut event_stream).await;
        assert_eq!(event.component_url(), url);
    }
}
