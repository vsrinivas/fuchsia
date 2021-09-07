// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    std::fs::{read_dir, DirEntry},
};

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init().unwrap();

    // Create the dynamic child
    let realm = connect_to_protocol::<fsys::RealmMarker>().unwrap();
    let mut collection_ref = fsys::CollectionRef { name: String::from("coll") };
    let child_decl = fsys::ChildDecl {
        name: Some(String::from("storage_user")),
        url: Some(String::from(
            "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_user.cm",
        )),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
        ..fsys::ChildDecl::EMPTY
    };

    realm
        .create_child(&mut collection_ref, child_decl, fsys::CreateChildArgs::EMPTY)
        .await
        .unwrap()
        .unwrap();

    // Bind to child
    let mut child_ref =
        fsys::ChildRef { name: "storage_user".to_string(), collection: Some("coll".to_string()) };
    let (exposed_dir, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

    realm.open_exposed_dir(&mut child_ref, server_end).await.unwrap().unwrap();
    let _ = fuchsia_component::client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(
        &exposed_dir,
    )
    .expect("failed to connect to fuchsia.component.Binder");

    let source = EventSource::new().unwrap();
    let mut event_stream = source.take_static_event_stream("TestEventStream").await.unwrap();

    // Expect the dynamic child to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./coll:storage_user:1")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

    // Destroy the child
    realm.destroy_child(&mut child_ref).await.unwrap().unwrap();

    // Expect the dynamic child to be purged
    EventMatcher::ok()
        .moniker("./coll:storage_user:1")
        .wait::<Purged>(&mut event_stream)
        .await
        .unwrap();

    // Ensure that memfs does not have a directory for the dynamic child
    let dir = read_dir("/hub/children/memfs/exec/out/svc/fuchsia.io.Directory").unwrap();
    let entries: Vec<DirEntry> = dir.map(|e| e.unwrap()).collect();
    assert!(entries.is_empty());
}
