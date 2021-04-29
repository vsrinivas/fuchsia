// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{Destroyed, Event, EventMode, EventSource, EventSubscription, Started},
        matcher::EventMatcher,
    },
    fuchsia_async as fasync,
    fuchsia_component_test::ScopedInstance,
    fuchsia_syslog as syslog,
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&["async_reporter"]).unwrap();

    // Track all the starting child components.
    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Started::NAME, Destroyed::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();

    let mut instances = vec![];
    let url =
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/stub_component.cm".to_string();
    for _ in 1..=3 {
        let scoped_instance = ScopedInstance::new("coll".to_string(), url.clone()).await.unwrap();
        instances.push(scoped_instance);
    }

    // Dropping instances destroys the children.
    drop(instances);

    for _ in 1..=3 {
        let event = EventMatcher::ok().expect_match::<Started>(&mut event_stream).await;
        assert_eq!(event.component_url(), url);
    }

    for _ in 1..=3 {
        let event = EventMatcher::ok().expect_match::<Destroyed>(&mut event_stream).await;
        assert_eq!(event.component_url(), url);
    }
}
