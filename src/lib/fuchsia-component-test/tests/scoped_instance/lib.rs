// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
    test_case::test_case,
};

#[test_case("#meta/realm_with_wait.cm"; "wait")]
#[test_case("#meta/realm.cm"; "no_wait")]
#[fuchsia::test(logging_tags = ["fuchsia_component_v2_test"])]
async fn scoped_instances(root_component: &'static str) {
    let builder = RealmBuilder::new().await.unwrap();
    let root =
        builder.add_child("root", root_component, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.sys2.EventSource"))
                .from(Ref::parent())
                .to(&root),
        )
        .await
        .unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventSourceMarker>().unwrap();

    let event_source = EventSource::from_proxy(proxy);

    let mut event_stream =
        event_source.subscribe(vec![EventSubscription::new(vec![Stopped::NAME])]).await.unwrap();

    instance.start_component_tree().await.unwrap();

    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./root")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
