// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    test_utils_lib::events::{Event, EventMatcher, EventSource, Resolved, Started},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Track all the starting child components.
    let event_source = EventSource::new_async()?;
    let mut event_stream = event_source.subscribe(vec![Resolved::NAME, Started::NAME]).await?;

    event_source.start_component_tree().await?;

    let echo = connect_to_service::<fecho::EchoMarker>()?;

    // This will trigger the resolution of the child.
    let realm = connect_to_service::<fsys::RealmMarker>()?;
    let mut child_ref = fsys::ChildRef { name: "child_a".to_string(), collection: None };

    let (_, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _ = realm.bind_child(&mut child_ref, server_end).await;

    let resolved_event = event_stream.expect_exact::<Resolved>(EventMatcher::new()).await?;
    // A started event should still be dispatched indicating failure due to a resolution
    // error.
    let started_event = event_stream.expect_exact::<Started>(EventMatcher::new()).await?;

    assert_eq!(resolved_event.error, started_event.error);

    if resolved_event.error.is_some() {
        let _ = echo.echo_string(Some("ERROR")).await?;
    } else {
        let _ = echo.echo_string(Some("PAYLOAD")).await?;
    }

    Ok(())
}
