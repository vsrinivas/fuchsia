// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        DEFAULT_COLLECTION_NAME,
    },
    futures::{channel::mpsc, sink::SinkExt, Future, FutureExt, StreamExt, TryStreamExt},
    maplit::hashset,
    moniker::{RelativeMoniker, RelativeMonikerBase},
    std::collections::HashSet,
};

/// Returns a new mock that writes a file and terminates. The future indicates when writing the file has completed.
fn new_data_user_mock<T: Into<String>, U: Into<String>>(
    filename: T,
    contents: U,
) -> (ComponentSource, impl Future<Output = ()>) {
    let (send, recv) = mpsc::channel(1);
    let filename = filename.into();
    let contents = contents.into();

    let mock = ComponentSource::mock(move |mock_handles| {
        let mut send_clone = send.clone();
        let filename_clone = filename.clone();
        let contents_clone = contents.clone();
        async move {
            let data_handle =
                mock_handles.clone_from_namespace("data").expect("data directory not available");

            let file = io_util::open_file(
                &data_handle,
                filename_clone.as_ref(),
                fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
            )
            .expect("failed to open file");
            io_util::write_file(&file, &contents_clone).await.expect("write file failed");
            send_clone.send(()).await.unwrap();
            Ok(())
        }
        .boxed()
    });
    (mock, recv.into_future().map(|_| ()))
}

/// Collect the set of component instances that use the `data` storage capability.
async fn collect_storage_user_monikers<T: AsRef<str>>(
    admin: &fsys::StorageAdminProxy,
    realm_moniker: T,
) -> HashSet<String> {
    let (it_proxy, it_server) =
        create_proxy::<fsys::StorageIteratorMarker>().expect("create iterator");
    admin
        .list_storage_in_realm(realm_moniker.as_ref(), it_server)
        .await
        .expect("fidl error")
        .expect("list storage error");

    let mut storage_monikers = hashset! {};
    loop {
        let next = it_proxy.next().await.expect("Error calling next on storage iterator");
        match next.is_empty() {
            true => break,
            false => storage_monikers.extend(next.into_iter()),
        }
    }
    storage_monikers
}

#[fasync::run_singlethreaded(test)]
async fn single_storage_user() {
    let (mock, done_signal) = new_data_user_mock("file", "data");
    let mut builder = RealmBuilder::new().await.unwrap();
    builder
        .add_eager_component("storage-user", mock)
        .await
        .unwrap()
        .add_route(CapabilityRoute {
            capability: Capability::storage("data", "/data"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("storage-user")],
        })
        .unwrap()
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("storage-user")],
        })
        .unwrap();
    let realm = builder.build();
    let instance = realm.create().await.unwrap();
    let _ = instance.root.connect_to_binder().unwrap();
    let instance_moniker = format!("./{}:{}", DEFAULT_COLLECTION_NAME, instance.root.child_name());
    let storage_user_moniker = format!("{}/storage-user", &instance_moniker);

    let storage_admin = connect_to_protocol::<fsys::StorageAdminMarker>().unwrap();
    let storage_users = collect_storage_user_monikers(&storage_admin, instance_moniker).await;
    assert_eq!(
        storage_users
            .iter()
            .map(|moniker_with_instances| RelativeMoniker::parse(&moniker_with_instances)
                .unwrap()
                .to_string_without_instances())
            .collect::<HashSet<_>>(),
        hashset! {
            storage_user_moniker.clone()
        }
    );

    done_signal.await;

    let (node_proxy, node_server) = create_proxy::<fio::NodeMarker>().expect("create node proxy");
    let dir_proxy = io_util::node_to_directory(node_proxy).unwrap();
    let storage_user_moniker_with_instances = storage_users.into_iter().next().unwrap();
    storage_admin
        .open_component_storage(
            &storage_user_moniker_with_instances,
            fio::OPEN_RIGHT_READABLE,
            0,
            node_server,
        )
        .expect("open component storage");
    let filenames: HashSet<_> = files_async::readdir_recursive(&dir_proxy, None)
        .map_ok(|dir_entry| dir_entry.name)
        .try_collect()
        .await
        .expect("Error reading directory");
    assert_eq!(filenames, hashset! {"file".to_string()});
    let file = io_util::open_file(&dir_proxy, "file".as_ref(), fio::OPEN_RIGHT_READABLE).unwrap();
    assert_eq!(io_util::file::read_to_string(&file).await.unwrap(), "data".to_string());
}

#[fasync::run_singlethreaded(test)]
async fn multiple_storage_users() {
    const NUM_MOCKS: usize = 5;
    let mut builder = RealmBuilder::new().await.unwrap();
    let (mocks, done_signals): (Vec<_>, Vec<_>) =
        (0..NUM_MOCKS).map(|_| new_data_user_mock("file", "data")).unzip();
    for (mock_idx, mock) in mocks.into_iter().enumerate() {
        let mock_name = format!("storage-user-{:?}", mock_idx);
        builder
            .add_eager_component(mock_name.as_str(), mock)
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::storage("data", "/data"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component(&mock_name)],
            })
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.logger.LogSink"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component(&mock_name)],
            })
            .unwrap();
    }

    let realm = builder.build();
    let instance = realm.create().await.unwrap();
    let _ = instance.root.connect_to_binder().unwrap();
    let instance_moniker = format!("./{}:{}", DEFAULT_COLLECTION_NAME, instance.root.child_name());
    let expected_storage_users: HashSet<_> = (0..NUM_MOCKS)
        .map(|mock_idx| format!("{}/storage-user-{:?}", &instance_moniker, mock_idx))
        .collect();

    let storage_admin = connect_to_protocol::<fsys::StorageAdminMarker>().unwrap();
    let storage_users = collect_storage_user_monikers(&storage_admin, instance_moniker).await;
    assert_eq!(
        storage_users
            .iter()
            .map(|moniker_with_instances| RelativeMoniker::parse(&moniker_with_instances)
                .unwrap()
                .to_string_without_instances())
            .collect::<HashSet<_>>(),
        expected_storage_users
    );

    futures::future::join_all(done_signals).await;
}
