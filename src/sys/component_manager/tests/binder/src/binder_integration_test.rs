// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client,
};

const HELLO_WORLD_MONIKER: &str = "hello_world";

#[fuchsia::test]
async fn test_component_starts_on_bind() {
    let event_source = EventSource::from_proxy(
        client::connect_to_protocol::<fsys::EventSourceMarker>()
            .expect("failed to connect to fuchsia.sys2.EventSource"),
    );

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Started::NAME], EventMode::Async)])
        .await
        .expect("failed to create event stream");

    let _ = client::connect_to_protocol::<fcomponent::BinderMarker>()
        .expect("failed to connect to fuchsia.component.Binder");

    EventMatcher::ok()
        .r#type(fsys::EventType::Started)
        .moniker(HELLO_WORLD_MONIKER.to_owned())
        .wait::<Started>(&mut event_stream)
        .await
        .expect("failed to observe events");
}
