// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::EventMatcher},
    fidl::endpoints,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io::DirectoryMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    hub_report::*,
};

#[fuchsia::component]
async fn main() {
    let event_source = EventSource::new().unwrap();

    // Subscribe to relevant events
    let mut event_stream =
        event_source.take_static_event_stream("DynamicChildEventStream").await.unwrap();

    // Create a dynamic child component
    let realm = connect_to_protocol::<fcomponent::RealmMarker>().unwrap();
    let mut collection_ref = fdecl::CollectionRef { name: String::from("coll") };
    let child_decl = fdecl::Child {
        name: Some(String::from("simple_instance")),
        url: Some(String::from("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/simple.cm")),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };

    realm
        .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
        .await
        .unwrap()
        .unwrap();

    expect_dir_listing("/hub/children", vec!["coll:simple_instance"]).await;
    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "id", "url"],
    )
    .await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec![]).await;
    expect_file_content("/hub/children/coll:simple_instance/id", "1").await;

    // Bind to the dynamic child
    let mut child_ref = fdecl::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };
    let (exposed_dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm.open_exposed_dir(&mut child_ref, server_end).await.unwrap().unwrap();
    let _ = fuchsia_component::client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(
        &exposed_dir,
    )
    .expect("failed to connect to fuchsia.component.Binder");

    let _ = EventMatcher::ok()
        .moniker_regex("./coll:simple_instance")
        .wait::<Started>(&mut event_stream)
        .await
        .expect("failed to wait for simple_instance to start");

    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "exec", "id", "resolved", "url"],
    )
    .await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec!["child"]).await;
    expect_file_content("/hub/children/coll:simple_instance/children/child/id", "0").await;

    // Delete the dynamic child
    let mut child_ref = fdecl::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };

    let destroy_task = fasync::Task::spawn(realm.destroy_child(&mut child_ref));

    // Wait for the dynamic child to stop
    let event = EventMatcher::ok()
        .moniker_regex("./coll:simple_instance")
        .expect_match::<Stopped>(&mut event_stream)
        .await;

    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "id", "resolved", "url"],
    )
    .await;

    event.resume().await.unwrap();

    // Wait for the dynamic child to begin deletion
    let event = EventMatcher::ok()
        .moniker_regex("./coll:simple_instance")
        .expect_match::<Destroyed>(&mut event_stream)
        .await;

    expect_dir_listing("/hub/children", vec![]).await;

    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to begin deletion
    let event = EventMatcher::ok()
        .moniker_regex("./coll:simple_instance/child")
        .expect_match::<Destroyed>(&mut event_stream)
        .await;

    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to be purged
    let event = EventMatcher::ok()
        .moniker_regex("./coll:simple_instance/child")
        .expect_match::<Purged>(&mut event_stream)
        .await;

    destroy_task.await.unwrap().unwrap();

    event.resume().await.unwrap();

    // Wait for the dynamic child to be purged
    let event = EventMatcher::ok()
        .moniker_regex("./coll:simple_instance")
        .expect_match::<Purged>(&mut event_stream)
        .await;

    event.resume().await.unwrap();
}
