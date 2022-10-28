// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
};

// This test is identical to storage_realm_coll, but the storage routing to `storage_user` is
// invalid. We want to confirm that the child is successfully stopped and destroyed in the situation
// where component manager is unable to find the storage the component wanted to use.

#[fasync::run_singlethreaded]
async fn main() {
    // Create the dynamic child
    let realm = connect_to_protocol::<fcomponent::RealmMarker>().unwrap();
    let mut collection_ref = fdecl::CollectionRef { name: String::from("coll_bad_route") };
    let child_decl = fdecl::Child {
        name: Some(String::from("storage_user")),
        url: Some(String::from("#meta/only_exits.cm")),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };

    realm
        .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
        .await
        .unwrap()
        .unwrap();

    // Start child
    let mut child_ref = fdecl::ChildRef {
        name: "storage_user".to_string(),
        collection: Some("coll_bad_route".to_string()),
    };
    let (exposed_dir, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

    realm.open_exposed_dir(&mut child_ref, server_end).await.unwrap().unwrap();
    let _ = fuchsia_component::client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(
        &exposed_dir,
    )
    .expect("failed to connect to fuchsia.component.Binder");

    let mut event_stream = EventStream::open().await.unwrap();

    // Expect the dynamic child to stop.
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./coll_bad_route:storage_user")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

    // Destroy the child
    realm.destroy_child(&mut child_ref).await.unwrap().unwrap();

    // Expect the dynamic child to be destroyed
    EventMatcher::ok()
        .moniker("./coll_bad_route:storage_user")
        .wait::<Destroyed>(&mut event_stream)
        .await
        .unwrap();
}
