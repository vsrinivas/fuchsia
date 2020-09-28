// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{self, DiscoverableService, Proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_sys2 as fsys, fidl_test_componentmanager_stresstests as fstresstests,
    fuchsia_async as fasync,
    fuchsia_component::client,
    futures::prelude::*,
    futures::stream,
    uuid::Uuid,
};

const NUM_CHILDREN: u16 = 1000;

// TODO(fxbug.dev/58641): enable this stress test
#[fasync::run_singlethreaded(test)]
#[ignore]
/// This stress test will create a 1000 children, make sure they are running and then stop them.
async fn launch_and_stress_test() {
    let realm = client::connect_to_service::<fsys::RealmMarker>().unwrap();
    let stream = stream::iter(0..NUM_CHILDREN);

    let lifecycles:Vec<fstresstests::LifecycleProxy> = stream.then(|_| async {
            let lifecycle = create_child(&realm, "children", "fuchsia-pkg://fuchsia.com/component-manager-stress-tests#meta/child-for-stress-test.cm").await;
            lifecycle
        }).collect::<Vec<_>>().await;

    stream::iter(lifecycles)
        .for_each_concurrent(None, |lifecycle| async move {
            match lifecycle.take_event_stream().try_next().await.unwrap().unwrap() {
                fstresstests::LifecycleEvent::OnConnected {} => {}
            }
            lifecycle.stop().unwrap();
            lifecycle.on_closed().await.unwrap();
        })
        .await;
}

async fn create_child(
    realm: &fsys::RealmProxy,
    collection: &str,
    url: &str,
) -> fstresstests::LifecycleProxy {
    let name = format!("{}", Uuid::new_v4().to_string());
    let mut collection_ref = fsys::CollectionRef { name: collection.to_string() };
    let child_decl = fsys::ChildDecl {
        name: Some(name.clone()),
        url: Some(url.to_string()),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
    };
    realm.create_child(&mut collection_ref, child_decl).await.unwrap().unwrap();
    let mut child_ref = fsys::ChildRef { name, collection: Some(collection.to_string()) };
    let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm.bind_child(&mut child_ref, server_end).await.unwrap().unwrap();
    let (lifecycle, lifecycle_request) =
        endpoints::create_proxy::<fstresstests::LifecycleMarker>().unwrap();
    connect_request_to_protocol_at_dir(&dir, lifecycle_request);
    lifecycle
}

/// Connect to an instance of a FIDL protocol hosted in `directory` to `server_end`.
fn connect_request_to_protocol_at_dir<S: DiscoverableService>(
    directory: &DirectoryProxy,
    server_end: ServerEnd<S>,
) {
    let path = format!("svc/{}", S::SERVICE_NAME);
    directory
        .open(
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
            fidl_fuchsia_io::MODE_TYPE_SERVICE,
            &path,
            ServerEnd::new(server_end.into_channel()),
        )
        .unwrap();
}
