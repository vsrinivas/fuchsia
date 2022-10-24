// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
};

// TODO(http://fxbug.dev/91100): Deduplicate this function. It is used in other CM integration tests
async fn start_nested_cm_and_wait_for_clean_stop(root_url: &str, moniker_to_wait_on: &str) {
    let builder = RealmBuilder::new().await.unwrap();
    let root = builder.add_child("root", root_url, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.sys2.EventSource"))
                .capability(Capability::event_stream("started_v2").with_scope(&root))
                .capability(Capability::event_stream("stopped_v2").with_scope(&root))
                .capability(Capability::event_stream("destroyed_v2").with_scope(&root))
                .capability(Capability::event_stream("directory_ready_v2").with_scope(&root))
                .capability(Capability::event_stream("capability_requested_v2").with_scope(&root))
                .capability(Capability::event_stream("resolved_v2").with_scope(&root))
                .capability(
                    Capability::event_stream("directory_ready_v2").as_("directory_ready_unscoped"),
                )
                .from(Ref::parent())
                .to(&root),
        )
        .await
        .unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventStream2Marker>().unwrap();

    let mut event_stream = EventStream::new_v2(proxy);

    instance.start_component_tree().await.unwrap();

    // Expect the component to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker(moniker_to_wait_on)
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn from_framework_should_not_work() {
    let root_url = "#meta/async_reporter_v2.cm";
    let moniker_to_wait_on = "./root";
    let builder = RealmBuilder::new().await.unwrap();
    let root = builder.add_child("root", root_url, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.sys2.EventSource"))
                .capability(Capability::event_stream("started_v2").with_scope(&root))
                .capability(Capability::event_stream("stopped_v2").with_scope(&root))
                .capability(Capability::event_stream("destroyed_v2").with_scope(&root))
                .capability(Capability::event_stream("directory_ready_v2").with_scope(&root))
                .capability(Capability::event_stream("capability_requested_v2").with_scope(&root))
                .capability(Capability::event_stream("resolved_v2").with_scope(&root))
                .capability(
                    Capability::event_stream("directory_ready_v2").as_("directory_ready_unscoped"),
                )
                .from(Ref::framework())
                .to(&root),
        )
        .await
        .unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventStream2Marker>().unwrap();

    let mut event_stream = EventStream::new_v2(proxy);

    instance.start_component_tree().await.unwrap();

    // Expect the component to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::AnyCrash))
        .moniker(moniker_to_wait_on)
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn async_event_source_test() {
    start_nested_cm_and_wait_for_clean_stop("#meta/async_reporter_v2.cm", "./root").await;
}

#[fasync::run_singlethreaded(test)]
async fn scoped_events_test() {
    start_nested_cm_and_wait_for_clean_stop("#meta/echo_realm_v2.cm", "./root/echo_reporter").await;
}

#[fasync::run_singlethreaded(test)]
async fn realm_offered_event_source_test() {
    start_nested_cm_and_wait_for_clean_stop(
        "#meta/realm_offered_root_v2.cm",
        "./root/nested_realm/reporter",
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn nested_event_source_test() {
    start_nested_cm_and_wait_for_clean_stop("#meta/nested_reporter_v2.cm", "./root").await;
}

#[fasync::run_singlethreaded(test)]
async fn event_directory_ready() {
    start_nested_cm_and_wait_for_clean_stop("#meta/directory_ready_root_v2.cm", "./root").await;
}

#[fasync::run_singlethreaded(test)]
async fn resolved_error_test() {
    start_nested_cm_and_wait_for_clean_stop("#meta/resolved_error_reporter_v2.cm", "./root").await;
}

#[fasync::run_singlethreaded(test)]
async fn synthesis_test() {
    start_nested_cm_and_wait_for_clean_stop("#meta/synthesis_reporter_v2.cm", "./root").await;
}
