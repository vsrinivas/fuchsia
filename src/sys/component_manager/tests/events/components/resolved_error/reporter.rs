// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as syslog,
    test_utils_lib::events::{Event, EventMatcher, EventSource, Resolved, Started},
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&["resolved_error_reporter"]).unwrap();

    // Track all the starting child components.
    let event_source = EventSource::new_async().unwrap();
    let mut event_stream =
        event_source.subscribe(vec![Resolved::NAME, Started::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    let echo = connect_to_service::<fecho::EchoMarker>().unwrap();

    // This will trigger the resolution of the child.
    let realm = connect_to_service::<fsys::RealmMarker>().unwrap();
    let mut child_ref = fsys::ChildRef { name: "child_a".to_string(), collection: None };

    let (_, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _ = realm.bind_child(&mut child_ref, server_end).await;

    let _resolved_event = event_stream.expect_match::<Resolved>(EventMatcher::err()).await;

    // A started event should still be dispatched indicating failure due to a resolution
    // error.
    let _started_event = event_stream.expect_match::<Started>(EventMatcher::err()).await;

    let _ = echo.echo_string(Some("ERROR")).await.unwrap();
}
