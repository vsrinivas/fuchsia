// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync,
};

#[fasync::run_singlethreaded(test)]
async fn component_manager_namespace() {
    let event_source = EventSource::new().unwrap();

    let mut event_stream =
        event_source.take_static_event_stream("StoppedEventStream").await.unwrap();

    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex("./nested_component_manager")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
