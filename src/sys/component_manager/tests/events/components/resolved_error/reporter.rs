// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{Event, EventMode, EventSource, EventSubscription, Resolved, Started},
        matcher::EventMatcher,
    },
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_protocol, connect_to_protocol_at_dir_root},
    fuchsia_syslog as syslog,
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&["resolved_error_reporter"]).unwrap();

    // Track all the starting child components.
    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Resolved::NAME, Started::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // This will trigger the resolution of the child.
    let realm = connect_to_protocol::<fsys::RealmMarker>().unwrap();
    let mut child_ref = fsys::ChildRef { name: "child_a".to_string(), collection: None };

    let (exposed_dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _ = realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .expect("failed to open exposed dir");

    // Attempt to start the child component should fail.
    assert!(connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir).is_err());

    let _resolved_event = EventMatcher::err().expect_match::<Resolved>(&mut event_stream).await;
}
