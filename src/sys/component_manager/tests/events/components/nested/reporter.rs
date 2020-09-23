// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as syslog,
    test_utils_lib::events::{Event, EventMatcher, EventSource, Handler, Started},
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&["nested_reporter"]).unwrap();

    // Track all the starting child components.
    let event_source = EventSource::new_sync().unwrap();
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    let echo = connect_to_service::<fecho::EchoMarker>().unwrap();

    for _ in 1..=3 {
        let event = event_stream.expect_match::<Started>(EventMatcher::ok()).await;
        let target_moniker = event.target_moniker();
        let _ = echo.echo_string(Some(target_moniker)).await;
        event.resume().await.unwrap();
    }
}
