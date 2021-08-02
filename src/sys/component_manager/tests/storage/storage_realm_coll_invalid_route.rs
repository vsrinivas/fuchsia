// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
};

// This test is identical to storage_realm_coll, but the storage routing to `storage_user` is
// invalid. We want to confirm that the child is successfully stopped and purged in the situation
// where component manager is unable to find the storage the component wanted to use.

#[fasync::run_singlethreaded]
async fn main() {
    // Create the dynamic child
    let realm = connect_to_protocol::<fsys::RealmMarker>().unwrap();
    let mut collection_ref = fsys::CollectionRef { name: String::from("coll_bad_route") };
    let child_decl = fsys::ChildDecl {
        name: Some(String::from("storage_user")),
        url: Some(String::from(
            "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/only_exits.cm",
        )),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
        ..fsys::ChildDecl::EMPTY
    };

    let child_args =
        fsys::CreateChildArgs { numbered_handles: None, ..fsys::CreateChildArgs::EMPTY };
    realm.create_child(&mut collection_ref, child_decl, child_args).await.unwrap().unwrap();

    // Bind to child
    let mut child_ref = fsys::ChildRef {
        name: "storage_user".to_string(),
        collection: Some("coll_bad_route".to_string()),
    };
    let (_, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

    realm.bind_child(&mut child_ref, server_end).await.unwrap().unwrap();

    let source = EventSource::new().unwrap();
    let mut event_stream = source.take_static_event_stream("TestEventStream").await.unwrap();

    // Expect the dynamic child to stop.
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./coll_bad_route:storage_user:1")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

    // Destroy the child
    realm.destroy_child(&mut child_ref).await.unwrap().unwrap();

    // Expect the dynamic child to be purged
    EventMatcher::ok()
        .moniker("./coll_bad_route:storage_user:1")
        .wait::<Purged>(&mut event_stream)
        .await
        .unwrap();
}
