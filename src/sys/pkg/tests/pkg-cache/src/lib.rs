// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::anyhow,
    assert_matches::assert_matches,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl::endpoints::DiscoverableProtocolMarker as _,
    fidl_fuchsia_io as fio, fidl_fuchsia_pkg as fpkg, fidl_fuchsia_pkg_ext as fpkg_ext,
    fidl_fuchsia_space as fspace, fidl_fuchsia_update as fupdate, fuchsia_async as fasync,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_inspect::{reader::DiagnosticsHierarchy, testing::TreeAssertion},
    fuchsia_merkle::Hash,
    fuchsia_pkg_testing::{get_inspect_hierarchy, BlobContents, Package},
    fuchsia_zircon::{self as zx, Status},
    futures::{future::BoxFuture, prelude::*},
    mock_boot_arguments::MockBootArgumentsService,
    mock_metrics::MockMetricEventLoggerFactory,
    mock_paver::{MockPaverService, MockPaverServiceBuilder},
    mock_verifier::MockVerifierService,
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc, time::Duration},
    vfs::directory::{entry::DirectoryEntry as _, helper::DirectlyMutable as _},
};

mod base_pkg_index;
mod cache_pkg_index;
mod cobalt;
mod executability_enforcement;
mod get;
mod inspect;
mod open;
mod pkgfs;
mod retained_packages;
mod space;
mod sync;

async fn write_blob(contents: &[u8], file: fio::FileProxy) -> Result<(), zx::Status> {
    let () =
        file.resize(contents.len() as u64).await.unwrap().map_err(zx::Status::from_raw).unwrap();

    fuchsia_fs::file::write(&file, contents).await.map_err(|e| match e {
        fuchsia_fs::file::WriteError::WriteError(s) => s,
        _ => zx::Status::INTERNAL,
    })?;

    let () = file.close().await.unwrap().map_err(zx::Status::from_raw).unwrap();
    Ok(())
}

// Calls fuchsia.pkg/NeededBlobs.GetMissingBlobs and reads from the iterator until it ends.
// Will block unless the package being cached does not have any uncached subpackage meta.fars (or
// another process is writing the subpackage meta.fars via NeededBlobs.OpenBlob), since pkg-cache
// can't determine which blobs are required to cache a package without reading the meta.fars of all
// the subpackages.
async fn get_missing_blobs(proxy: &fpkg::NeededBlobsProxy) -> Vec<fpkg::BlobInfo> {
    let (blob_iterator, blob_iterator_server_end) =
        fidl::endpoints::create_proxy::<fpkg::BlobInfoIteratorMarker>().unwrap();
    let () = proxy.get_missing_blobs(blob_iterator_server_end).unwrap();

    let mut res = vec![];
    loop {
        let chunk = blob_iterator.next().await.unwrap();
        if chunk.is_empty() {
            break;
        }
        res.extend(chunk);
    }
    res
}

// Verifies that:
//   1. all requested blobs are actually needed by the package
//   2. no blob is requested more than once
async fn get_and_verify_package(
    package_cache: &fpkg::PackageCacheProxy,
    pkg: &Package,
) -> fio::DirectoryProxy {
    let mut meta_blob_info = fpkg::BlobInfo {
        blob_id: fpkg_ext::BlobId::from(*pkg.meta_far_merkle_root()).into(),
        length: 0,
    };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<fpkg::NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    let (meta_far, _) = pkg.contents();
    let available_blobs = pkg.content_and_subpackage_blobs().unwrap();

    let () = write_meta_far(&needed_blobs, meta_far).await;
    let () = write_needed_blobs(&needed_blobs, available_blobs).await;

    let () = get_fut.await.unwrap().unwrap();
    let () = pkg.verify_contents(&dir).await.unwrap();
    dir
}

pub async fn write_meta_far(needed_blobs: &fpkg::NeededBlobsProxy, meta_far: BlobContents) {
    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());
    write_blob(&meta_far.contents, meta_blob).await.unwrap();
}

pub async fn write_needed_blobs(
    needed_blobs: &fpkg::NeededBlobsProxy,
    mut available_blobs: HashMap<Hash, Vec<u8>>,
) {
    let (blob_iterator, blob_iterator_server_end) =
        fidl::endpoints::create_proxy::<fpkg::BlobInfoIteratorMarker>().unwrap();
    let () = needed_blobs.get_missing_blobs(blob_iterator_server_end).unwrap();

    loop {
        let chunk = blob_iterator.next().await.unwrap();
        if chunk.is_empty() {
            break;
        }
        for mut blob_info in chunk {
            let (blob_proxy, blob_server_end) =
                fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
            assert!(needed_blobs
                .open_blob(&mut blob_info.blob_id, blob_server_end)
                .await
                .unwrap()
                .unwrap());
            let () = write_blob(
                available_blobs
                    .remove(&fpkg_ext::BlobId::from(blob_info.blob_id).into())
                    .unwrap()
                    .as_slice(),
                blob_proxy,
            )
            .await
            .unwrap();
        }
    }
}

// Calls PackageCache.Get and verifies the package directory for each element of `packages`
// concurrently.
// PackageCache.Get requires that the caller not write the same blob concurrently across
// separate calls and this fn does not enforce that, so this fn should not be called with
// packages that share blobs.
async fn get_and_verify_packages(proxy: &fpkg::PackageCacheProxy, packages: &[Package]) {
    let () = futures::stream::iter(packages)
        .for_each_concurrent(None, move |pkg| get_and_verify_package(proxy, pkg).map(|_| {}))
        .await;
}

// Verifies that:
//   1. `pkg` can be opened via PackageCache.Get without needing to write any blobs.
//   2. `pkg` matches the package directory obtained from PackageCache.Get.
//
// Does *not* verify that `pkg`'s subpackages were cached, as this would requiring using
// PackageCache.Get on them which would activate them in the dynamic index.
//
// Returns the package directory obtained from PackageCache.Get.
async fn verify_package_cached(
    proxy: &fpkg::PackageCacheProxy,
    pkg: &Package,
) -> fio::DirectoryProxy {
    let mut meta_blob_info = fpkg::BlobInfo {
        blob_id: fpkg_ext::BlobId::from(*pkg.meta_far_merkle_root()).into(),
        length: 0,
    };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<fpkg::NeededBlobsMarker>().unwrap();

    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

    let get_fut = proxy
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let (_meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

    // If the package is active in the dynamic index, the server will send a `ZX_OK` epitaph then
    // close the channel.
    // If all the blobs are cached but the package is not active in the dynamic index the server
    // will reply with `Ok(false)`, meaning that the metadata blob is cached and GetMissingBlobs
    // needs to be performed (but the iterator obtained with GetMissingBlobs should be empty).
    let epitaph_received = match needed_blobs.open_meta_blob(meta_blob_server_end).await {
        Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }) => true,
        Ok(Ok(false)) => false,
        Ok(r) => {
            panic!("Meta blob not cached: unexpected response {:?}", r)
        }
        Err(e) => {
            panic!("Meta blob not cached: unexpected FIDL error {:?}", e)
        }
    };

    let (blob_iterator, blob_iterator_server_end) =
        fidl::endpoints::create_proxy::<fpkg::BlobInfoIteratorMarker>().unwrap();
    let missing_blobs_response = needed_blobs.get_missing_blobs(blob_iterator_server_end);

    if epitaph_received {
        // The rust FIDL bindings that send an epitaph do not immediately close the channel
        // (see fxbug.dev/81036).
        // Since NeededBlobs.GetMissingBlobs has no return value, the rust bindings just write a
        // message to the channel and do not wait for a response.
        // Therefore, it's possible for the client's GetMissingBlobs message to race with the
        // server's closing of the channel after writing the epitaph.
        // If the client does manage to send the GetMissingBlobs message before the server closes
        // the channel, the blob iterator server end will be closed (by the kernel as part of
        // cleaning up the zircon channel).
        // pkg-cache does not immediately drop the NeededBlobs request stream after writing the
        // epitaph, so this race condition is perhaps slightly more likely to occur than normal.
        // https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/bin/pkg-cache/src/cache_service.rs;l=226;drc=74e6e2b5ca618c0d9caa8ab07f64eba96f4ce922
        match missing_blobs_response {
            // Channel closed before GetMissingBlob request was sent.
            Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }) => {}
            // GetMissingBlob request sent before channel was closed, the iterator channel
            // should be closed.
            Ok(()) => {
                assert_matches!(
                    blob_iterator.next().await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
            }
            Err(e) => {
                panic!("Content blobs not cached: unexpected error {:?}", e)
            }
        }
    } else {
        // All subpackage meta.fars and content blobs should be cached, so iterator should be empty.
        assert_eq!(blob_iterator.next().await.unwrap(), vec![]);
    }

    let () = get_fut.await.unwrap().unwrap();

    // `dir` is resolved to package directory.
    let () = pkg.verify_contents(&dir).await.unwrap();

    dir
}

pub async fn replace_retained_packages(
    proxy: &fpkg::RetainedPackagesProxy,
    packages: &[fpkg_ext::BlobId],
) {
    let packages = packages.iter().cloned().map(Into::into).collect::<Vec<_>>();
    let (iterator_client_end, iterator_stream) =
        fidl::endpoints::create_request_stream::<fpkg::BlobIdIteratorMarker>().unwrap();
    let serve_iterator_fut = async {
        fpkg_ext::serve_fidl_iterator_from_slice(iterator_stream, packages).await.unwrap();
    };
    let (replace_retained_result, ()) =
        futures::join!(proxy.replace(iterator_client_end), serve_iterator_fut);
    assert_matches!(replace_retained_result, Ok(()));
}

async fn verify_packages_cached(proxy: &fpkg::PackageCacheProxy, packages: &[Package]) {
    let () = futures::stream::iter(packages)
        .for_each_concurrent(None, move |pkg| verify_package_cached(proxy, pkg).map(|_| ()))
        .await;
}

trait Blobfs {
    fn root_proxy(&self) -> fio::DirectoryProxy;
}

impl Blobfs for BlobfsRamdisk {
    fn root_proxy(&self) -> fio::DirectoryProxy {
        self.root_dir_proxy().unwrap()
    }
}

struct TestEnvBuilder<BlobfsAndSystemImageFut> {
    paver_service_builder: Option<MockPaverServiceBuilder>,
    blobfs_and_system_image: BlobfsAndSystemImageFut,
    ignore_system_image: bool,
    enable_subpackages: bool,
}

impl TestEnvBuilder<BoxFuture<'static, (BlobfsRamdisk, Option<Hash>)>> {
    fn new() -> Self {
        Self {
            blobfs_and_system_image: async {
                let system_image_package =
                    fuchsia_pkg_testing::SystemImageBuilder::new().build().await;
                let blobfs = BlobfsRamdisk::start().unwrap();
                let () = system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
                (blobfs, Some(*system_image_package.meta_far_merkle_root()))
            }
            .boxed(),
            paver_service_builder: None,
            ignore_system_image: false,
            enable_subpackages: false,
        }
    }
}

impl<BlobfsAndSystemImageFut, ConcreteBlobfs> TestEnvBuilder<BlobfsAndSystemImageFut>
where
    BlobfsAndSystemImageFut: Future<Output = (ConcreteBlobfs, Option<Hash>)>,
    ConcreteBlobfs: Blobfs,
{
    fn paver_service_builder(self, paver_service_builder: MockPaverServiceBuilder) -> Self {
        Self { paver_service_builder: Some(paver_service_builder), ..self }
    }

    fn blobfs_and_system_image_hash<OtherBlobfs>(
        self,
        blobfs: OtherBlobfs,
        system_image: Option<Hash>,
    ) -> TestEnvBuilder<future::Ready<(OtherBlobfs, Option<Hash>)>>
    where
        OtherBlobfs: Blobfs,
    {
        TestEnvBuilder {
            blobfs_and_system_image: future::ready((blobfs, system_image)),
            paver_service_builder: self.paver_service_builder,
            ignore_system_image: self.ignore_system_image,
            enable_subpackages: self.enable_subpackages,
        }
    }

    /// Creates a BlobfsRamdisk loaded with, and configures pkg-cache to use, the supplied
    /// `system_image` package.
    fn blobfs_from_system_image(
        self,
        system_image: &Package,
    ) -> TestEnvBuilder<future::Ready<(BlobfsRamdisk, Option<Hash>)>> {
        self.blobfs_from_system_image_and_extra_packages(system_image, &[])
    }

    /// Creates a BlobfsRamdisk loaded with the supplied packages and configures the system to use
    /// the supplied `system_image` package.
    fn blobfs_from_system_image_and_extra_packages(
        self,
        system_image: &Package,
        extra_packages: &[&Package],
    ) -> TestEnvBuilder<future::Ready<(BlobfsRamdisk, Option<Hash>)>> {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let root_dir = blobfs.root_dir().unwrap();
        let () = system_image.write_to_blobfs_dir(&root_dir);
        for pkg in extra_packages {
            let () = pkg.write_to_blobfs_dir(&root_dir);
        }

        TestEnvBuilder::<_> {
            blobfs_and_system_image: future::ready((
                blobfs,
                Some(*system_image.meta_far_merkle_root()),
            )),
            paver_service_builder: self.paver_service_builder,
            ignore_system_image: self.ignore_system_image,
            enable_subpackages: self.enable_subpackages,
        }
    }

    fn ignore_system_image(self) -> Self {
        assert_eq!(self.ignore_system_image, false);
        Self { ignore_system_image: true, ..self }
    }

    fn enable_subpackages(self) -> Self {
        assert_eq!(self.enable_subpackages, false);
        Self { enable_subpackages: true, ..self }
    }

    async fn build(self) -> TestEnv<ConcreteBlobfs> {
        let (blobfs, system_image) = self.blobfs_and_system_image.await;
        let local_child_svc_dir = vfs::pseudo_directory! {};

        // Cobalt mocks so we can assert that we emit the correct events
        let logger_factory = Arc::new(MockMetricEventLoggerFactory::new());
        {
            let logger_factory = Arc::clone(&logger_factory);
            local_child_svc_dir
                .add_entry(
                    fidl_fuchsia_metrics::MetricEventLoggerFactoryMarker::PROTOCOL_NAME,
                    vfs::service::host(move |stream| {
                        Arc::clone(&logger_factory).run_logger_factory(stream)
                    }),
                )
                .unwrap();
        }

        // Paver service, so we can verify that we submit the expected requests and so that
        // we can verify if the paver service returns errors, that we handle them correctly.
        let paver_service = Arc::new(
            self.paver_service_builder.unwrap_or_else(|| MockPaverServiceBuilder::new()).build(),
        );
        {
            let paver_service = Arc::clone(&paver_service);
            local_child_svc_dir
                .add_entry(
                    fidl_fuchsia_paver::PaverMarker::PROTOCOL_NAME,
                    vfs::service::host(move |stream| {
                        Arc::clone(&paver_service).run_paver_service(stream).unwrap_or_else(|e| {
                            panic!("error running paver service: {:#}", anyhow!(e))
                        })
                    }),
                )
                .unwrap();
        }

        // Set up verifier service so we can verify that we reject GC until after the verifier
        // commits this boot/slot as successful, lest we break rollbacks.
        let verifier_service = Arc::new(MockVerifierService::new(|_| Ok(())));
        {
            let verifier_service = Arc::clone(&verifier_service);
            local_child_svc_dir
                .add_entry(
                    fidl_fuchsia_update_verify::BlobfsVerifierMarker::PROTOCOL_NAME,
                    vfs::service::host(move |stream| {
                        Arc::clone(&verifier_service).run_blobfs_verifier_service(stream)
                    }),
                )
                .unwrap();
        }

        // fuchsia.boot/Arguments service to supply the hash of the system_image package.
        let mut arguments_service = MockBootArgumentsService::new(HashMap::new());
        system_image.map(|hash| arguments_service.insert_pkgfs_boot_arg(hash));
        let arguments_service = Arc::new(arguments_service);
        {
            let arguments_service = Arc::clone(&arguments_service);
            local_child_svc_dir
                .add_entry(
                    fidl_fuchsia_boot::ArgumentsMarker::PROTOCOL_NAME,
                    vfs::service::host(move |stream| {
                        Arc::clone(&arguments_service).handle_request_stream(stream)
                    }),
                )
                .unwrap();
        }

        let local_child_out_dir = vfs::pseudo_directory! {
            "blob" => vfs::remote::remote_dir(blobfs.root_proxy()),
            "svc" => local_child_svc_dir,
        };

        let local_child_out_dir = Mutex::new(Some(local_child_out_dir));

        let pkg_cache_manifest = if self.ignore_system_image {
            "#meta/pkg-cache-ignore-system-image.cm"
        } else {
            "#meta/pkg-cache.cm"
        };

        let builder = RealmBuilder::new().await.unwrap();
        let pkg_cache =
            builder.add_child("pkg_cache", pkg_cache_manifest, ChildOptions::new()).await.unwrap();
        if self.enable_subpackages {
            builder.init_mutable_config_from_package(&pkg_cache).await.unwrap();
            builder.set_config_value_bool(&pkg_cache, "enable_subpackages", true).await.unwrap();
        }
        let system_update_committer = builder
            .add_child("system_update_committer", "fuchsia-pkg://fuchsia.com/pkg-cache-integration-tests#meta/system-update-committer.cm", ChildOptions::new()).await.unwrap();
        let service_reflector = builder
            .add_local_child(
                "service_reflector",
                move |handles| {
                    let local_child_out_dir = local_child_out_dir
                        .lock()
                        .take()
                        .expect("mock component should only be launched once");
                    let scope = vfs::execution_scope::ExecutionScope::new();
                    let () = local_child_out_dir.open(
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
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&pkg_cache)
                    .to(&service_reflector)
                    .to(&system_update_committer),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.metrics.MetricEventLoggerFactory",
                    ))
                    .capability(Capability::protocol_by_name("fuchsia.boot.Arguments"))
                    .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                    .capability(
                        Capability::directory("blob-exec")
                            .path("/blob")
                            .rights(fio::RW_STAR_DIR | fio::Operations::EXECUTE),
                    )
                    .from(&service_reflector)
                    .to(&pkg_cache),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.paver.Paver"))
                    .capability(Capability::protocol_by_name(
                        "fuchsia.update.verify.BlobfsVerifier",
                    ))
                    .from(&service_reflector)
                    .to(&system_update_committer),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.update.CommitStatusProvider"))
                    .from(&system_update_committer)
                    .to(&pkg_cache) // offer
                    .to(Ref::parent()), // expose
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageCache"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.RetainedPackages"))
                    .capability(Capability::protocol_by_name("fuchsia.space.Manager"))
                    .capability(Capability::directory("pkgfs"))
                    .capability(Capability::directory("system"))
                    .capability(Capability::directory("pkgfs-packages"))
                    .capability(Capability::directory("pkgfs-versions"))
                    .from(&pkg_cache)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        let realm_instance = builder.build().await.unwrap();

        let proxies = Proxies {
            commit_status_provider: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<fupdate::CommitStatusProviderMarker>()
                .expect("connect to commit status provider"),
            space_manager: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<fspace::ManagerMarker>()
                .expect("connect to space manager"),
            package_cache: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<fpkg::PackageCacheMarker>()
                .expect("connect to package cache"),
            retained_packages: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<fpkg::RetainedPackagesMarker>()
                .expect("connect to retained packages"),
            pkgfs_packages: fuchsia_fs::directory::open_directory_no_describe(
                realm_instance.root.get_exposed_dir(),
                "pkgfs-packages",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            )
            .expect("open pkgfs-packages"),
            pkgfs_versions: fuchsia_fs::directory::open_directory_no_describe(
                realm_instance.root.get_exposed_dir(),
                "pkgfs-versions",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            )
            .expect("open pkgfs-versions"),
            pkgfs: fuchsia_fs::directory::open_directory_no_describe(
                realm_instance.root.get_exposed_dir(),
                "pkgfs",
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::RIGHT_EXECUTABLE,
            )
            .expect("open pkgfs"),
        };

        TestEnv {
            apps: Apps { realm_instance },
            blobfs,
            system_image,
            proxies,
            mocks: Mocks {
                logger_factory,
                _paver_service: paver_service,
                _verifier_service: verifier_service,
            },
        }
    }
}

struct Proxies {
    commit_status_provider: fupdate::CommitStatusProviderProxy,
    space_manager: fspace::ManagerProxy,
    package_cache: fpkg::PackageCacheProxy,
    retained_packages: fpkg::RetainedPackagesProxy,
    pkgfs_packages: fio::DirectoryProxy,
    pkgfs_versions: fio::DirectoryProxy,
    pkgfs: fio::DirectoryProxy,
}

pub struct Mocks {
    pub logger_factory: Arc<MockMetricEventLoggerFactory>,
    _paver_service: Arc<MockPaverService>,
    _verifier_service: Arc<MockVerifierService>,
}

struct Apps {
    realm_instance: RealmInstance,
}

struct TestEnv<B = BlobfsRamdisk> {
    apps: Apps,
    blobfs: B,
    system_image: Option<Hash>,
    proxies: Proxies,
    pub mocks: Mocks,
}

impl TestEnv<BlobfsRamdisk> {
    // workaround for fxbug.dev/38162
    async fn stop(self) {
        // Tear down the environment in reverse order, ending with the storage.
        drop(self.proxies);
        drop(self.apps);
        self.blobfs.stop().await.unwrap();
    }
}

impl TestEnv<BlobfsRamdisk> {
    fn builder() -> TestEnvBuilder<BoxFuture<'static, (BlobfsRamdisk, Option<Hash>)>> {
        TestEnvBuilder::new()
    }
}

impl<B: Blobfs> TestEnv<B> {
    async fn inspect_hierarchy(&self) -> DiagnosticsHierarchy {
        let nested_environment_label = format!(
            "pkg_cache_integration_test/realm_builder\\:{}",
            self.apps.realm_instance.root.child_name()
        );

        get_inspect_hierarchy(&nested_environment_label, "pkg_cache").await
    }

    pub async fn open_package(&self, merkle: &str) -> Result<fio::DirectoryProxy, zx::Status> {
        let (package, server_end) = fidl::endpoints::create_proxy().unwrap();
        let status_fut = self
            .proxies
            .package_cache
            .open(&mut merkle.parse::<fpkg_ext::BlobId>().unwrap().into(), server_end);

        let () = status_fut.await.unwrap().map_err(zx::Status::from_raw)?;
        Ok(package)
    }

    async fn block_until_started(&self) {
        let (_, server_end) = fidl::endpoints::create_endpoints().unwrap();
        // The fidl call should succeed, but the result of open doesn't matter.
        let _ = self
            .proxies
            .package_cache
            .open(
                &mut "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse::<fpkg_ext::BlobId>()
                    .unwrap()
                    .into(),
                server_end,
            )
            .await
            .unwrap();

        // Also, make sure the system-update-committer starts to prevent race conditions
        // where the system-update-commiter drops before the paver.
        let _ = self.proxies.commit_status_provider.is_current_system_committed().await.unwrap();
    }

    /// Wait until pkg-cache inspect state satisfies `desired_state`, return the satisfying state.
    pub async fn wait_for_and_return_inspect_state(
        &self,
        desired_state: TreeAssertion<String>,
    ) -> DiagnosticsHierarchy {
        loop {
            let hierarchy = self.inspect_hierarchy().await;
            if desired_state.run(&hierarchy).is_ok() {
                break hierarchy;
            }
            fasync::Timer::new(Duration::from_millis(10)).await;
        }
    }

    pub fn client(&self) -> fidl_fuchsia_pkg_ext::cache::Client {
        fidl_fuchsia_pkg_ext::cache::Client::from_proxy(self.proxies.package_cache.clone())
    }

    /// Get a DirectoryProxy to pkg-cache's exposed /system directory.
    /// This proxy is not stored in Proxies because the directory is not served when there is no
    /// system_image package.
    async fn system_dir(&self) -> fio::DirectoryProxy {
        fuchsia_fs::directory::open_directory(
            self.apps.realm_instance.root.get_exposed_dir(),
            "system",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .await
        .expect("open system")
    }
}
