// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
};

/// Starts a nested component manager with a given root component URL using Realm Builer.
/// Routes some capabilities like LogSink and EventSource to the root.
async fn start_nested_cm(cm_url: &str, root_url: &str) -> RealmInstance {
    let builder = RealmBuilder::new().await.unwrap();
    let root = builder.add_child("root", root_url, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::event_stream("destroyed_v2").with_scope(&root))
                .capability(Capability::event_stream("stopped_v2").with_scope(&root))
                .from(Ref::parent())
                .to(&root),
        )
        .await
        .unwrap();
    builder.build_in_nested_component_manager(cm_url).await.unwrap()
}

/// Connects to the EventSource protocol from the nested component manager, starts the component
/// topology and waits for a clean stop of the specified component instance
async fn wait_for_clean_stop(cm: RealmInstance, moniker_to_wait_on: &str) {
    let proxy = cm.root.connect_to_protocol_at_exposed_dir::<fsys::EventStream2Marker>().unwrap();

    let mut event_stream = EventStream::new_v2(proxy);

    cm.start_component_tree().await.unwrap();

    // Expect the component to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker(moniker_to_wait_on)
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn storage() {
    let cm = start_nested_cm("#meta/component_manager.cm", "#meta/storage_realm.cm").await;
    wait_for_clean_stop(cm, "./root/storage_user").await;
}

#[fasync::run_singlethreaded(test)]
async fn storage_from_collection() {
    let cm = start_nested_cm("#meta/component_manager.cm", "#meta/storage_realm_coll.cm").await;
    wait_for_clean_stop(cm, "./root").await;
}

#[fasync::run_singlethreaded(test)]
async fn storage_from_collection_with_invalid_route() {
    let cm =
        start_nested_cm("#meta/component_manager.cm", "#meta/storage_realm_coll_invalid_route.cm")
            .await;
    wait_for_clean_stop(cm, "./root").await;
}
