// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::EventMatcher},
    fidl::endpoints,
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog as syslog,
    hub_report::*,
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init().unwrap();
    let event_source = EventSource::new().unwrap();

    // Subscribe to relevant events
    let mut event_stream =
        event_source.take_static_event_stream("DynamicChildEventStream").await.unwrap();

    // Create a dynamic child component
    let realm = connect_to_protocol::<fsys::RealmMarker>().unwrap();
    let mut collection_ref = fsys::CollectionRef { name: String::from("coll") };
    let child_decl = fsys::ChildDecl {
        name: Some(String::from("simple_instance")),
        url: Some(String::from("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/simple.cm")),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
        ..fsys::ChildDecl::EMPTY
    };

    let child_args =
        fsys::CreateChildArgs { numbered_handles: None, ..fsys::CreateChildArgs::EMPTY };
    realm.create_child(&mut collection_ref, child_decl, child_args).await.unwrap().unwrap();

    expect_dir_listing("/hub/children", vec!["coll:simple_instance"]).await;
    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "deleting", "id", "url"],
    )
    .await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec![]).await;
    expect_file_content("/hub/children/coll:simple_instance/id", "1").await;

    // Bind to the dynamic child
    let mut child_ref = fsys::ChildRef {
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
        .moniker("./coll:simple_instance:1")
        .wait::<Started>(&mut event_stream)
        .await
        .expect("failed to wait for simple_instance to start");

    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "deleting", "exec", "id", "resolved", "url"],
    )
    .await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec!["child"]).await;
    expect_file_content("/hub/children/coll:simple_instance/children/child/id", "0").await;

    // Delete the dynamic child
    let mut child_ref = fsys::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };

    let destroy_task = fasync::Task::spawn(realm.destroy_child(&mut child_ref));

    // Wait for the dynamic child to stop
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1")
        .expect_match::<Stopped>(&mut event_stream)
        .await;

    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "deleting", "id", "resolved", "url"],
    )
    .await;

    event.resume().await.unwrap();

    // Wait for the dynamic child to begin deletion
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1")
        .expect_match::<Destroyed>(&mut event_stream)
        .await;

    expect_dir_listing("/hub/children", vec![]).await;
    expect_dir_listing("/hub/deleting", vec!["coll:simple_instance:1"]).await;
    expect_dir_listing(
        "/hub/deleting/coll:simple_instance:1",
        vec!["children", "component_type", "debug", "deleting", "id", "resolved", "url"],
    )
    .await;

    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to begin deletion
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1/child:0")
        .expect_match::<Destroyed>(&mut event_stream)
        .await;

    expect_dir_listing("/hub/deleting/coll:simple_instance:1/children", vec![]).await;
    expect_dir_listing("/hub/deleting/coll:simple_instance:1/deleting", vec!["child:0"]).await;

    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to be purged
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1/child:0")
        .expect_match::<Purged>(&mut event_stream)
        .await;

    destroy_task.await.unwrap().unwrap();

    expect_dir_listing("/hub/deleting/coll:simple_instance:1/deleting", vec![]).await;

    event.resume().await.unwrap();

    // Wait for the dynamic child to be purged
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1")
        .expect_match::<Purged>(&mut event_stream)
        .await;

    expect_dir_listing("/hub/deleting", vec![]).await;

    event.resume().await.unwrap();
}
