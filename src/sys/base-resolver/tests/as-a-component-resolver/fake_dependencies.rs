// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::DiscoverableProtocolMarker as _,
    fidl_fuchsia_boot as fboot, fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{
        PackageCacheMarker, PackageCacheRequest, PackageCacheRequestStream, PackageIndexEntry,
        PackageIndexIteratorRequest, PackageIndexIteratorRequestStream, PackageUrl,
    },
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_url::UnpinnedAbsolutePackageUrl,
    futures::prelude::*,
    maplit::hashmap,
    std::collections::HashMap,
    std::sync::Arc,
    tracing::{error, info},
    vfs::directory::entry::DirectoryEntry as _,
};

static PKGFS_BOOT_ARG_KEY: &'static str = "zircon.system.pkgfs.cmd";
static PKGFS_BOOT_ARG_VALUE_PREFIX: &'static str = "bin/pkgsvr+";

static MOCK_PACKAGE_HASH: &'static str =
    "facefacefacefacefacefacefacefacefacefacefacefacefacefacefaceface";

// The pkg-cache-resolver integration test demonstrates that the pkg-cache-resolver resolves a
// a working (has executable blobs, responds to FIDL requests from capabilities routed by the
// component manager) component and that the base-resolver.cml manifest is correct, but it doesn't
// need to resolve the actual pkg-cache (because whether the component resolved is the pkg-cache is
// determined by what meta.far happens to be at the hash in blobfs that the pkg-cache-resolver
// reads from the system_image package which is already made up in this test). So, because
// the actual pkg-cache has a complicated environment that it requires to start successfully, we
// just have the pkg-cache-resolver resolve the same mock ping-pong component that the
// base-resolver test uses.
// To make that work, we make a copy of the mock component manifest and store it at
// meta/pkg-cache.cm (the path that the pkg-cache-resolver expects) in the test package, and then
// write the entire test package to a fake blobfs, along with a fake system_image package and its
// data/static_packages file, which points the pkg-cache-resolver at the "pkg-cache" meta.far, which
// is actually the meta.far for the test package all this is running from.

#[fuchsia::main]
async fn main() {
    info!("started");

    let this_pkg = fuchsia_pkg_testing::Package::identity().await.unwrap();

    let static_packages = system_image::StaticPackages::from_entries(vec![(
        "pkg-cache/0".parse().unwrap(),
        *this_pkg.meta_far_merkle_root(),
    )]);
    let mut static_packages_bytes = vec![];
    static_packages.serialize(&mut static_packages_bytes).expect("write static_packages");
    let system_image = fuchsia_pkg_testing::PackageBuilder::new("system_image")
        .add_resource_at("data/static_packages", static_packages_bytes.as_slice())
        .build()
        .await
        .expect("build system_image package");
    let blobfs = blobfs_ramdisk::BlobfsRamdisk::start().expect("start blobfs");
    let () = system_image.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let () = this_pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    // Use VFS because ServiceFs does not support OPEN_RIGHT_EXECUTABLE, but /blob needs it.
    let system_image_hash = *system_image.meta_far_merkle_root();
    let out_dir = vfs::pseudo_directory! {
        "svc" => vfs::pseudo_directory! {
            fboot::ArgumentsMarker::PROTOCOL_NAME =>
                vfs::service::host(move |stream: fboot::ArgumentsRequestStream| {
                    serve_boot_args(stream, system_image_hash)
                }),
            PackageCacheMarker::PROTOCOL_NAME =>
                vfs::service::host(move |stream: PackageCacheRequestStream| {
                    let mock_package_url: UnpinnedAbsolutePackageUrl =
                        "fuchsia-pkg://fuchsia.com/mock-package".parse().unwrap();
                    let mock_package_blob_id = BlobId::parse(MOCK_PACKAGE_HASH).unwrap();
                    let base_package_index = hashmap! {
                        mock_package_url => mock_package_blob_id,
                    };
                    let cache = MockPackageCacheService::new_with_base_packages(Arc::new(base_package_index));
                    cache.run_service(stream)
                }),
        },
        "blob" =>
            vfs::remote::remote_dir(blobfs.root_dir_proxy().expect("get blobfs root dir")),
        "pkgfs-packages" => vfs::pseudo_directory! {
            "mock-package" => vfs::pseudo_directory! {
                "0" => vfs::remote::remote_dir(fuchsia_fs::directory::open_in_namespace(
                    "/pkg",
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
                )
                .unwrap()),
            },
        },
    };

    let scope = vfs::execution_scope::ExecutionScope::new();
    out_dir.open(
        scope.clone(),
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
        fio::MODE_TYPE_DIRECTORY,
        vfs::path::Path::dot(),
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .unwrap()
            .into(),
    );
    let () = scope.wait().await;
}

async fn serve_boot_args(mut stream: fboot::ArgumentsRequestStream, hash: fuchsia_hash::Hash) {
    while let Some(request) = stream.try_next().await.unwrap() {
        match request {
            fboot::ArgumentsRequest::GetString { key, responder } => {
                assert_eq!(key, PKGFS_BOOT_ARG_KEY);
                responder.send(Some(&format!("{}{}", PKGFS_BOOT_ARG_VALUE_PREFIX, hash))).unwrap();
            }
            req => panic!("unexpected request {:?}", req),
        }
    }
}

// The actual pkg-cache will fit as many items per chunk as possible.  Intentionally choose a
// small, fixed value here to verify the BasePackageIndex behavior with multiple chunks without
// having to actually send hundreds of entries in these tests.
const PACKAGE_INDEX_CHUNK_SIZE: u32 = 30;

struct MockPackageCacheService {
    base_packages: Arc<HashMap<UnpinnedAbsolutePackageUrl, BlobId>>,
}

impl MockPackageCacheService {
    fn new_with_base_packages(
        base_packages: Arc<HashMap<UnpinnedAbsolutePackageUrl, BlobId>>,
    ) -> Self {
        Self { base_packages }
    }

    async fn run_service(self, mut stream: PackageCacheRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                PackageCacheRequest::BasePackageIndex { iterator, control_handle: _ } => {
                    let iterator = iterator.into_stream().unwrap();
                    self.serve_package_iterator(iterator);
                }
                _ => panic!("unexpected PackageCache request: {:?}", req),
            }
        }
    }

    fn serve_package_iterator(&self, mut stream: PackageIndexIteratorRequestStream) {
        let mut packages = self
            .base_packages
            .iter()
            .map(|(url, &hash)| PackageIndexEntry {
                package_url: PackageUrl { url: url.to_string() },
                meta_far_blob_id: hash.into(),
            })
            .collect::<Vec<PackageIndexEntry>>();

        fasync::Task::spawn(
            async move {
                let mut iter = packages.iter_mut();
                while let Some(request) = stream.try_next().await? {
                    let PackageIndexIteratorRequest::Next { responder } = request;

                    responder.send(&mut iter.by_ref().take(PACKAGE_INDEX_CHUNK_SIZE as usize))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: fidl::Error| {
                error!("while serving package index iterator: {:?}", e)
            }),
        )
        .detach();
    }
}
