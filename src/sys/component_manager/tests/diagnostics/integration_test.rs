// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component_test::{ChildOptions, RealmBuilder, RouteBuilder, RouteEndpoint},
};

async fn start_nested_cm_and_wait_for_clean_stop(root_url: &str, moniker_to_wait_on: &str) {
    let builder = RealmBuilder::new().await.unwrap();
    builder.add_child("root", root_url, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.LogSink")
                .source(RouteEndpoint::above_root())
                .targets(vec![RouteEndpoint::component("root")]),
        )
        .await
        .unwrap();
    builder
        .add_route(
            RouteBuilder::protocol("fuchsia.sys2.EventSource")
                .source(RouteEndpoint::above_root())
                .targets(vec![RouteEndpoint::component("root")]),
        )
        .await
        .unwrap();
    builder
        .add_route(
            RouteBuilder::event(
                fuchsia_component_test::Event::DirectoryReady("diagnostics".to_string()),
                cm_rust::EventMode::Async,
            )
            .source(RouteEndpoint::AboveRoot)
            .targets(vec![RouteEndpoint::component("root")]),
        )
        .await
        .unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventSourceMarker>().unwrap();

    let event_source = EventSource::from_proxy(proxy);

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect the component to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex(moniker_to_wait_on)
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fuchsia::test]
async fn component_manager_exposes_inspect() {
    start_nested_cm_and_wait_for_clean_stop(
        "#meta/component-manager-inspect.cm",
        "./root/reporter",
    )
    .await;
}

#[fuchsia::test]
async fn cleanup_of_components_without_diagnostics() {
    start_nested_cm_and_wait_for_clean_stop("#meta/cleanup-root.cm", "./root/cleanup").await;
}
