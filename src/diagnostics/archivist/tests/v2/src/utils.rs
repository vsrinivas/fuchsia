// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use component_events::{events::*, matcher::*};
use fidl_fuchsia_sys2::EventSourceMarker;
use fuchsia_component::client;

pub async fn wait_for_component_stopped(
    instance_child_name: &str,
    component: &str,
    status_match: ExitStatusMatcher,
) {
    let event_source =
        EventSource::from_proxy(client::connect_to_protocol::<EventSourceMarker>().unwrap());
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();
    wait_for_component_stopped_event(
        instance_child_name,
        component,
        status_match,
        &mut event_stream,
    )
    .await;
}

pub async fn wait_for_component_stopped_event(
    instance_child_name: &str,
    component: &str,
    status_match: ExitStatusMatcher,
    event_stream: &mut EventStream,
) {
    let moniker_for_match = format!(
        "./fuchsia_component_test_collection:{}:\\d+/test:\\d+/{}:\\d+",
        instance_child_name, component
    );
    EventMatcher::ok()
        .stop(Some(status_match))
        .moniker(moniker_for_match)
        .wait::<Stopped>(event_stream)
        .await
        .unwrap();
}
