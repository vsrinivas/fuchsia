// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
};

#[fasync::run_singlethreaded(test)]
async fn shutdown_test() {
    let builder = RealmBuilder::new().await.unwrap();
    let root = builder
        .add_child("root", "#meta/shutdown_integration_root.cm", ChildOptions::new().eager())
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.sys2.SystemController"))
                .from(Ref::parent())
                .to(&root),
        )
        .await
        .unwrap();
    let _instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();

    let mut event_stream = EventStream::open().await.unwrap();

    // Expect component manager to stop cleanly
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
