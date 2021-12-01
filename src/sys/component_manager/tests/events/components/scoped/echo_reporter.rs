// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync,
};

#[fasync::run_singlethreaded]
async fn main() {
    let event_source = EventSource::new().unwrap();
    let mut event_stream =
        event_source.take_static_event_stream("ScopedEventStream").await.unwrap();
    EventMatcher::ok()
        .moniker_regex("./echo_server")
        .wait::<Started>(&mut event_stream)
        .await
        .unwrap();
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex("./echo_server")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
