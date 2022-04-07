// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    assert_matches::assert_matches,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::*,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    futures::{
        future::{BoxFuture, FutureExt as _},
        stream::TryStreamExt as _,
    },
    std::sync::Arc,
    vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope},
};

static BASE_RESOLVER_URL: &str =
    "fuchsia-pkg://fuchsia.com/base-resolver-integration-tests#meta/base-resolver.cm";
static PKGFS_BOOT_ARG_KEY: &'static str = "zircon.system.pkgfs.cmd";
static PKGFS_BOOT_ARG_VALUE_PREFIX: &'static str = "bin/pkgsvr+";
static PKG_CACHE_COMPONENT_URL: &'static str = "fuchsia-pkg-cache:///#meta/pkg-cache.cm";

trait BootArgumentsStreamHandler: Send + Sync {
    fn handle_stream(
        &self,
        stream: fidl_fuchsia_boot::ArgumentsRequestStream,
    ) -> BoxFuture<'static, ()>;
}

struct TestEnvBuilder {
    blobfs: Arc<dyn DirectoryEntry>,
    boot_args: Arc<dyn BootArgumentsStreamHandler>,
}

impl TestEnvBuilder {
    fn new(
        blobfs: Arc<dyn DirectoryEntry>,
        boot_args: Arc<dyn BootArgumentsStreamHandler>,
    ) -> Self {
        Self { blobfs, boot_args }
    }

    async fn build(self) -> TestEnv {
        let blobfs = self.blobfs;
        let boot_args = self.boot_args;

        let builder = RealmBuilder::new().await.unwrap();

        let base_resolver = builder
            .add_child("base_resolver", BASE_RESOLVER_URL, ChildOptions::new())
            .await
            .unwrap();

        let local_mocks = builder
            .add_local_child(
                "local_mocks",
                move |handles| {
                    let blobfs_clone = blobfs.clone();
                    let boot_args_clone = boot_args.clone();
                    let out_dir = vfs::pseudo_directory! {
                        "blob" => blobfs_clone,
                        "svc" => vfs::pseudo_directory! {
                            "fuchsia.boot.Arguments" =>
                                vfs::service::host(move |stream|
                                    boot_args_clone.handle_stream(stream)
                                ),
                        },
                    };
                    let scope = ExecutionScope::new();
                    let () = out_dir.open(
                        scope.clone(),
                        fio::OpenFlags::RIGHT_READABLE
                            | fio::OpenFlags::RIGHT_WRITABLE
                            | fio::OpenFlags::RIGHT_EXECUTABLE,
                        0,
                        vfs::path::Path::dot(),
                        handles.outgoing_dir.into_channel().into(),
                    );
                    async move { Ok(scope.wait().await) }.boxed()
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("blob").path("/blob").rights(fio::RX_STAR_DIR),
                    )
                    .capability(
                        Capability::directory("pkgfs-packages")
                            .path("/pkgfs-packages")
                            .rights(fio::RX_STAR_DIR),
                    )
                    .capability(Capability::protocol_by_name("fuchsia.boot.Arguments"))
                    .from(&local_mocks)
                    .to(&base_resolver),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&base_resolver),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.sys2.ComponentResolver-ForPkgCache",
                    ))
                    .from(&base_resolver)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        TestEnv { realm_instance: builder.build().await.unwrap() }
    }
}

struct TestEnv {
    realm_instance: RealmInstance,
}

impl TestEnv {
    fn pkg_cache_resolver(&self) -> ComponentResolverProxy {
        self.realm_instance
            .root
            .connect_to_named_protocol_at_exposed_dir::<ComponentResolverMarker>(
                "fuchsia.sys2.ComponentResolver-ForPkgCache",
            )
            .unwrap()
    }
}

// Responds to requests for "zircon.system.pkgfs.cmd" with the provided hash.
struct BootArgsFixedHash {
    hash: fuchsia_hash::Hash,
}

impl BootArgsFixedHash {
    fn new(hash: fuchsia_hash::Hash) -> Self {
        Self { hash }
    }
}

impl BootArgumentsStreamHandler for BootArgsFixedHash {
    fn handle_stream(
        &self,
        mut stream: fidl_fuchsia_boot::ArgumentsRequestStream,
    ) -> BoxFuture<'static, ()> {
        let hash = self.hash;
        async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    fidl_fuchsia_boot::ArgumentsRequest::GetString { key, responder } => {
                        assert_eq!(key, PKGFS_BOOT_ARG_KEY);
                        responder
                            .send(Some(&format!("{}{}", PKGFS_BOOT_ARG_VALUE_PREFIX, hash)))
                            .unwrap();
                    }
                    req => panic!("unexpected request {:?}", req),
                }
            }
        }
        .boxed()
    }
}

// The test package these tests are in includes the pkg-cache component. This function creates
// a BlobfsRamdisk and copies into it:
//   1. this test package (and therefore everything needed for pkg-cache)
//   2. a system_image package with a data/static_packages file that says the hash of pkg-cache
//      is the hash of this test package
//
// This function also creates a BootArgsFixedHash that reports the hash of the system_image
// package as that of the system_image package this function wrote to the returned BlobfsRamdisk.
async fn create_blobfs_and_boot_args_from_this_package() -> (BlobfsRamdisk, BootArgsFixedHash) {
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
    let blobfs = BlobfsRamdisk::start().expect("start blobfs");
    let () = system_image.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let () = this_pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    (blobfs, BootArgsFixedHash::new(*system_image.meta_far_merkle_root()))
}

#[fuchsia::test]
async fn pkg_cache_resolver_succeeds() {
    let (blobfs, boot_args) = create_blobfs_and_boot_args_from_this_package().await;

    let env = TestEnvBuilder::new(
        vfs::remote::remote_dir(blobfs.root_dir_proxy().unwrap()),
        Arc::new(boot_args),
    )
    .build()
    .await;

    assert_matches!(
        env.pkg_cache_resolver().resolve(PKG_CACHE_COMPONENT_URL).await.unwrap(),
        Ok(Component { .. })
    );
}
