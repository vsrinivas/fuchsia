// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component_test::new::{ChildOptions, RealmBuilder},
};

#[fuchsia::test]
async fn base_resolver_appmgr_bridge_test() {
    let builder = RealmBuilder::new().await.unwrap();
    builder
        .add_child(
            "echo_server",
            "fuchsia-pkg://fuchsia.com/base_resolver_test#meta/echo_server.cm",
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/cm_appmgr_loader.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventSourceMarker>().unwrap();

    let event_source = EventSource::from_proxy(proxy);

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Started::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect realm builder root to start
    EventMatcher::ok().moniker_regex(".").expect_match::<Started>(&mut event_stream).await;

    // Expect start to succeed because we're using the appmgr loader
    EventMatcher::ok()
        .moniker_regex("./echo_server")
        .expect_match::<Started>(&mut event_stream)
        .await;
}

#[fuchsia::test]
/// This uses the root_component.rs implementation to make the test package's
/// contents appear at /pkgfs. This allows component manager's built-in base
/// package resolver to see the contents of the package. HOWEVER, the component
/// manager configuration here sets the built-in resolver to 'None', meaning we
/// expect the attempt to start `echo_server` to not resolve.
async fn base_resolver_disabled_test() {
    let builder = RealmBuilder::new().await.unwrap();
    builder.add_child("root", "#meta/root.cm", ChildOptions::new().eager()).await.unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/cm_disabled_resolver.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventSourceMarker>().unwrap();

    let event_source = EventSource::from_proxy(proxy);

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Started::NAME, Resolved::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect the root component to be bound to
    let _ = EventMatcher::ok()
        .moniker_regex("./root")
        .wait::<Started>(&mut event_stream)
        .await
        .unwrap();

    // Expect start failure for echo_server because we shouldn't resolve the component
    EventMatcher::err()
        .moniker_regex("./root/echo_server")
        .expect_match::<Resolved>(&mut event_stream)
        .await;
}
