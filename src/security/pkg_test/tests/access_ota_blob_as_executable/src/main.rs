// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_EXECUTABLE,
        OPEN_RIGHT_READABLE, VMO_FLAG_EXEC, VMO_FLAG_READ,
    },
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_pkg::{BlobId, PackageCacheMarker, PackageResolverMarker},
    fidl_fuchsia_sys2::{StorageAdminMarker, StorageIteratorMarker},
    fidl_test_security_pkg::PackageServer_Marker,
    files_async::readdir,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_merkle::MerkleTree,
    fuchsia_zircon::{AsHandleRef, Rights, Status},
    futures::join,
    io_util::directory::open_file,
    std::convert::TryInto,
    std::fs::File,
};

const HELLO_WORLD_V0_META_FAR_PATH: &str = "/pkg/data/assemblies/v0/hello_world/meta.far";

async fn get_storage_for_component_instance(moniker_prefix: &str) -> DirectoryProxy {
    let storage_admin = connect_to_protocol::<StorageAdminMarker>().unwrap();
    let (storage_user_iterator, storage_user_iterator_server_end) =
        create_proxy::<StorageIteratorMarker>().unwrap();
    storage_admin
        .list_storage_in_realm(".", storage_user_iterator_server_end)
        .await
        .unwrap()
        .unwrap();
    let mut matching_storage_users = vec![];
    loop {
        let chunk = storage_user_iterator.next().await.unwrap();
        if chunk.is_empty() {
            break;
        }
        let mut matches: Vec<String> =
            chunk.into_iter().filter(|moniker| moniker.starts_with(moniker_prefix)).collect();
        matching_storage_users.append(&mut matches);
    }
    assert_eq!(1, matching_storage_users.len());
    let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();
    storage_admin
        .open_component_storage(
            matching_storage_users.first().unwrap(),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            ServerEnd::new(server_end.into_channel()),
        )
        .unwrap();
    proxy
}

async fn get_executable_hello_world_v0_bin(hello_world_proxy: &DirectoryProxy) -> Box<Buffer> {
    let bin_file = open_file(
        hello_world_proxy,
        "bin/hello_world_v0",
        OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
    )
    .await
    .unwrap();
    let (status, result) = bin_file.get_buffer(VMO_FLAG_READ | VMO_FLAG_EXEC).await.unwrap();
    Status::ok(status).unwrap();
    let bin_buf = result.unwrap();
    let bin_info = bin_buf.vmo.basic_info().unwrap();
    assert_eq!(bin_info.rights & Rights::READ, Rights::READ);
    assert_eq!(bin_info.rights & Rights::EXECUTE, Rights::EXECUTE);
    bin_buf
}

async fn check_v0_cache_resolver_results() {
    // Determine the "name" (merkle root hash) of `hello_world_v0`.
    let mut hello_world_v0 = File::open(HELLO_WORLD_V0_META_FAR_PATH).unwrap();
    let hello_world_v0_merkle = MerkleTree::from_reader(&mut hello_world_v0).unwrap().root();
    let mut hello_world_v0_merkle = BlobId { merkle_root: hello_world_v0_merkle.into() };

    // Open hello_world_v0 package as executable via pkg-cache API.
    let pkg_cache_proxy = connect_to_protocol::<PackageCacheMarker>().unwrap();
    let (hello_world_proxy, hello_world_server_end) = create_proxy::<DirectoryMarker>().unwrap();
    pkg_cache_proxy
        .open(&mut hello_world_v0_merkle, hello_world_server_end)
        .await
        .unwrap()
        .unwrap();
    let cache_buffer = get_executable_hello_world_v0_bin(&hello_world_proxy).await;

    // Resolve hello_world_v0 package as executable via pkg-resolver API.
    let (hello_world_proxy, hello_world_server_end) = create_proxy::<DirectoryMarker>().unwrap();
    connect_to_protocol::<PackageResolverMarker>()
        .unwrap()
        .resolve("fuchsia-pkg://fuchsia.com/hello_world", hello_world_server_end)
        .await
        .unwrap()
        .unwrap();
    let resolver_buffer = get_executable_hello_world_v0_bin(&hello_world_proxy).await;

    // Expect resolver to return same bytes as cache.
    assert_eq!(cache_buffer.size, resolver_buffer.size);
    let mut cache_vec = vec![0; cache_buffer.size.try_into().unwrap()];
    let cache_data = cache_vec.as_mut_slice();
    cache_buffer.vmo.read(cache_data, 0).unwrap();
    let mut resolver_vec = vec![0; resolver_buffer.size.try_into().unwrap()];
    let resolver_data = resolver_vec.as_mut_slice();
    resolver_buffer.vmo.read(resolver_data, 0).unwrap();
    assert_eq!(cache_data, resolver_data);
}

async fn get_local_package_server_url() -> String {
    connect_to_protocol::<PackageServer_Marker>().unwrap().get_url().await.unwrap()
}

#[fuchsia::test]
async fn access_ota_blob_as_executable() {
    // Setup storage capabilities.
    let pkg_resolver_storage_proxy = get_storage_for_component_instance("./pkg-resolver").await;
    // TODO(fxbug.dev/88453): Need a test that confirms assumption: Production
    // configuration is an empty mutable storage directory.
    assert!(readdir(&pkg_resolver_storage_proxy).await.unwrap().is_empty());

    let (_, url) = join!(check_v0_cache_resolver_results(), get_local_package_server_url());

    // Placeholder assertion for well-formed local URL. Test will eventually use
    // URL to configure network connection for `pkg-resolver`.
    assert!(url.starts_with("http://localhost"));
}
