// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{anyhow, Error},
    blobfs_ramdisk::BlobfsRamdisk,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_cobalt::CobaltEvent,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, FileMarker, FileProxy},
    fidl_fuchsia_io2::Operations,
    fidl_fuchsia_pkg::{
        BlobIdIteratorMarker, BlobInfo, BlobInfoIteratorMarker, NeededBlobsMarker,
        NeededBlobsProxy, PackageCacheMarker, PackageCacheProxy, RetainedPackagesMarker,
        RetainedPackagesProxy,
    },
    fidl_fuchsia_pkg_ext::{serve_fidl_iterator, BlobId},
    fidl_fuchsia_space::{ManagerMarker as SpaceManagerMarker, ManagerProxy as SpaceManagerProxy},
    fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderProxy},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        RealmInstance,
    },
    fuchsia_inspect::{reader::DiagnosticsHierarchy, testing::TreeAssertion},
    fuchsia_merkle::Hash,
    fuchsia_pkg::{MetaContents, PackagePath},
    fuchsia_pkg_testing::{get_inspect_hierarchy, BlobContents, Package, SystemImageBuilder},
    fuchsia_zircon::{self as zx, Status},
    futures::{future::BoxFuture, prelude::*},
    io_util::file::*,
    maplit::hashmap,
    matches::assert_matches,
    mock_paver::{MockPaverService, MockPaverServiceBuilder},
    mock_verifier::MockVerifierService,
    parking_lot::Mutex,
    pkgfs_ramdisk::PkgfsRamdisk,
    std::{
        collections::HashMap,
        fs::{create_dir, create_dir_all, remove_dir, File},
        io::Write as _,
        path::PathBuf,
        sync::Arc,
        time::Duration,
    },
    system_image::StaticPackages,
    tempfile::TempDir,
};

mod base_pkg_index;
mod cobalt;
mod get;
mod inspect;
mod open;
mod retained_packages;
mod space;
mod sync;

async fn write_blob(contents: &[u8], file: FileProxy) -> Result<(), zx::Status> {
    let s = file.truncate(contents.len() as u64).await.unwrap();
    assert_eq!(zx::Status::from_raw(s), zx::Status::OK);

    io_util::file::write(&file, contents).await.map_err(|e| match e {
        WriteError::WriteError(s) => s,
        _ => zx::Status::INTERNAL,
    })?;

    let s = file.close().await.unwrap();
    assert_eq!(zx::Status::from_raw(s), zx::Status::OK);
    Ok(())
}

async fn get_missing_blobs(proxy: &NeededBlobsProxy) -> Vec<BlobInfo> {
    let (blob_iterator, blob_iterator_server_end) =
        fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
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

async fn do_fetch(package_cache: &PackageCacheProxy, pkg: &Package) -> DirectoryProxy {
    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
    let get_fut = package_cache
        .get(
            &mut meta_blob_info,
            &mut std::iter::empty(),
            needed_blobs_server_end,
            Some(dir_server_end),
        )
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    let (meta_far, contents) = pkg.contents();

    write_meta_far(&needed_blobs, meta_far).await;
    write_needed_blobs(&needed_blobs, contents).await;

    let () = get_fut.await.unwrap().unwrap();
    let () = pkg.verify_contents(&dir).await.unwrap();
    dir
}

pub async fn write_meta_far(needed_blobs: &NeededBlobsProxy, meta_far: BlobContents) {
    let (meta_blob, meta_blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());
    write_blob(&meta_far.contents, meta_blob).await.unwrap();
}

pub async fn write_needed_blobs(needed_blobs: &NeededBlobsProxy, contents: Vec<BlobContents>) {
    let missing_blobs = get_missing_blobs(&needed_blobs).await;
    let mut contents = contents
        .into_iter()
        .map(|blob| (BlobId::from(blob.merkle), blob.contents))
        .collect::<HashMap<_, Vec<u8>>>();
    for mut blob in missing_blobs {
        let buf = contents.remove(&blob.blob_id.into()).unwrap();

        let (content_blob, content_blob_server_end) =
            fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        assert!(needed_blobs
            .open_blob(&mut blob.blob_id, content_blob_server_end)
            .await
            .unwrap()
            .unwrap());

        let () = write_blob(&buf, content_blob).await.unwrap();
    }
    assert_eq!(contents, Default::default());
}

async fn verify_fetches_succeed(proxy: &PackageCacheProxy, packages: &[Package]) {
    let () = futures::stream::iter(packages)
        .for_each_concurrent(None, move |pkg| do_fetch(proxy, pkg).map(|_| {}))
        .await;
}

async fn verify_package_cached(proxy: &PackageCacheProxy, pkg: &Package) {
    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();

    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

    let get_fut = proxy
        .get(
            &mut meta_blob_info,
            &mut std::iter::empty(),
            needed_blobs_server_end,
            Some(dir_server_end),
        )
        .map_ok(|res| res.map_err(Status::from_raw));

    let (_meta_blob, meta_blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();

    // If the package is fully cached, server will close the channel with a `ZX_OK` epitaph.
    // In some cases, server will reply with `Ok(false)`, meaning that the metadata
    // blob is cached, and the content blobs need to be validated.
    let channel_closed = match needed_blobs.open_meta_blob(meta_blob_server_end).await {
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
        fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
    let missing_blobs_response = needed_blobs.get_missing_blobs(blob_iterator_server_end);

    if channel_closed {
        // Since `get_missing_blobs()` FIDL protocol method has no return value, on
        // the call the client writes to the channel and doesn't wait for a response.
        // As a result, it's possible for server reply to race with channel closure,
        // and client can receive a reply containing a channel after the channel was closed.
        // Sending a channel through a closed channel closes the end of the channel sent
        // through the channel.
        match missing_blobs_response {
            // The package is already cached and server closed the channel with with a `ZX_OK`
            // epitaph.
            Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }) => {}
            Ok(()) => {
                // As a result of race condition, iterator channel is closed.
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
        // Expect empty iterator returned to ensure content blobs are cached.
        assert!(blob_iterator.next().await.unwrap().is_empty());
    }

    let () = get_fut.await.unwrap().unwrap();

    // `dir` is resolved to package directory.
    let () = pkg.verify_contents(&dir).await.unwrap();
}

pub async fn replace_retained_packages(
    proxy: &RetainedPackagesProxy,
    packages: &[fidl_fuchsia_pkg_ext::BlobId],
) {
    let packages = packages.iter().cloned().map(Into::into).collect::<Vec<_>>();
    let (iterator_client_end, iterator_stream) =
        fidl::endpoints::create_request_stream::<BlobIdIteratorMarker>().unwrap();
    let serve_iterator_fut = async {
        serve_fidl_iterator(packages, iterator_stream).await.unwrap();
    };
    let (replace_retained_result, ()) =
        futures::join!(proxy.replace(iterator_client_end), serve_iterator_fut);
    assert_matches!(replace_retained_result, Ok(()));
}

async fn verify_packages_cached(proxy: &PackageCacheProxy, packages: &[Package]) {
    let () = futures::stream::iter(packages)
        .for_each_concurrent(None, move |pkg| verify_package_cached(proxy, pkg))
        .await;
}

trait PkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;

    fn blobfs_root_proxy(&self) -> Result<DirectoryProxy, Error>;
}

impl PkgFs for PkgfsRamdisk {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        PkgfsRamdisk::root_dir_handle(self)
    }

    fn blobfs_root_proxy(&self) -> Result<DirectoryProxy, Error> {
        self.blobfs().root_dir_proxy()
    }
}

struct TestEnvBuilder<PkgFsFn, PkgFsFut>
where
    PkgFsFn: FnOnce() -> PkgFsFut,
    PkgFsFut: Future,
    PkgFsFut::Output: PkgFs,
{
    paver_service_builder: Option<MockPaverServiceBuilder>,
    pkgfs: PkgFsFn,
}

async fn make_default_pkgfs_ramdisk() -> PkgfsRamdisk {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap()
}

impl TestEnvBuilder<fn() -> BoxFuture<'static, PkgfsRamdisk>, BoxFuture<'static, PkgfsRamdisk>> {
    fn new() -> Self {
        Self { pkgfs: || make_default_pkgfs_ramdisk().boxed(), paver_service_builder: None }
    }
}

impl<PkgFsFn, PkgFsFut> TestEnvBuilder<PkgFsFn, PkgFsFut>
where
    PkgFsFn: FnOnce() -> PkgFsFut,
    PkgFsFut: Future,
    PkgFsFut::Output: PkgFs,
{
    fn paver_service_builder(self, paver_service_builder: MockPaverServiceBuilder) -> Self {
        Self { paver_service_builder: Some(paver_service_builder), ..self }
    }

    fn pkgfs<Pother>(
        self,
        pkgfs: Pother,
    ) -> TestEnvBuilder<impl FnOnce() -> future::Ready<Pother>, future::Ready<Pother>>
    where
        Pother: PkgFs + 'static,
    {
        TestEnvBuilder {
            pkgfs: || future::ready(pkgfs),
            paver_service_builder: self.paver_service_builder,
        }
    }

    async fn build(self) -> TestEnv<PkgFsFut::Output> {
        let mut fs = ServiceFs::new();

        let pkgfs = (self.pkgfs)().await;
        fs.add_remote("pkgfs", pkgfs.root_dir_handle().unwrap().into_proxy().unwrap());
        fs.add_remote("blob", pkgfs.blobfs_root_proxy().unwrap());

        // Cobalt mocks so we can assert that we emit the correct events
        let logger_factory = Arc::new(MockLoggerFactory::new());
        let logger_factory_clone = Arc::clone(&logger_factory);
        fs.dir("svc").add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&logger_factory_clone).run_logger_factory(stream))
                .detach()
        });

        // Paver service, so we can verify that we submit the expected requests and so that
        // we can verify if the paver service returns errors, that we handle them correctly.
        let paver_service = Arc::new(
            self.paver_service_builder.unwrap_or_else(|| MockPaverServiceBuilder::new()).build(),
        );
        let paver_service_clone = Arc::clone(&paver_service);
        fs.dir("svc").add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&paver_service_clone)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach()
        });

        // Set up verifier service so we can verify that we reject GC until after the verifier
        // commits this boot/slot as successful, lest we break rollbacks.
        let verifier_service = Arc::new(MockVerifierService::new(|_| Ok(())));
        let verifier_service_clone = Arc::clone(&verifier_service);
        fs.dir("svc").add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&verifier_service_clone).run_blobfs_verifier_service(stream),
            )
            .detach()
        });

        let fs_holder = Mutex::new(Some(fs));

        let mut builder = RealmBuilder::new().await.unwrap();
        builder
            .add_component("pkg_cache", ComponentSource::url("fuchsia-pkg://fuchsia.com/pkg-cache-integration-tests#meta/pkg-cache.cm")).await.unwrap()
            .add_component("system_update_committer", ComponentSource::url("fuchsia-pkg://fuchsia.com/pkg-cache-integration-tests#meta/system-update-committer.cm")).await.unwrap()
            .add_component("service_reflector", ComponentSource::mock(move |mock_handles| {
                let mut rfs = fs_holder.lock().take().expect("mock component should only be launched once");
                async {
                    rfs.serve_connection(mock_handles.outgoing_dir.into_channel()).unwrap();
                    fasync::Task::spawn(rfs.collect()).detach();
                    Ok(())
                }.boxed()
            })).await.unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.logger.LogSink"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![
                    RouteEndpoint::component("pkg_cache"),
                    RouteEndpoint::component("service_reflector"),
                    RouteEndpoint::component("system_update_committer"),
                ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.cobalt.LoggerFactory"),
                source: RouteEndpoint::component("service_reflector"),
                targets: vec![
                    RouteEndpoint::component("pkg_cache"),
                ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.tracing.provider.Registry"),
                source: RouteEndpoint::component("service_reflector"),
                targets: vec![
                    RouteEndpoint::component("pkg_cache"),
                ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.paver.Paver"),
                source: RouteEndpoint::component("service_reflector"),
                targets: vec![ RouteEndpoint::component("system_update_committer") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.update.verify.BlobfsVerifier"),
                source: RouteEndpoint::component("service_reflector"),
                targets: vec![ RouteEndpoint::component("system_update_committer") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::directory("pkgfs", "/pkgfs", Operations::Connect | Operations::Enumerate | Operations::Traverse | Operations::ReadBytes | Operations::WriteBytes | Operations::ModifyDirectory | Operations::GetAttributes | Operations::UpdateAttributes),
                source: RouteEndpoint::component("service_reflector"),
                targets: vec![ RouteEndpoint::component("pkg_cache") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::directory("blob", "/blob", Operations::Connect | Operations::Enumerate | Operations::Traverse | Operations::ReadBytes | Operations::WriteBytes | Operations::ModifyDirectory | Operations::GetAttributes | Operations::UpdateAttributes),
                source: RouteEndpoint::component("service_reflector"),
                targets: vec![ RouteEndpoint::component("pkg_cache") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.update.CommitStatusProvider"),
                source: RouteEndpoint::component("system_update_committer"),
                targets: vec![
                    RouteEndpoint::component("pkg_cache"), // offer
                    RouteEndpoint::AboveRoot, // expose
                ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.pkg.PackageCache"),
                source: RouteEndpoint::component("pkg_cache"),
                targets: vec![ RouteEndpoint::AboveRoot ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.pkg.RetainedPackages"),
                source: RouteEndpoint::component("pkg_cache"),
                targets: vec![ RouteEndpoint::AboveRoot ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.space.Manager"),
                source: RouteEndpoint::component("pkg_cache"),
                targets: vec![ RouteEndpoint::AboveRoot ],
            }).unwrap();

        let realm_instance = builder.build().create().await.unwrap();

        let proxies = Proxies {
            commit_status_provider: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<CommitStatusProviderMarker>()
                .expect("connect to commit status provider"),
            space_manager: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<SpaceManagerMarker>()
                .expect("connect to space manager"),
            package_cache: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<PackageCacheMarker>()
                .expect("connect to package cache"),
            retained_packages: realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<RetainedPackagesMarker>()
                .expect("connect to retained packages"),
        };

        TestEnv {
            apps: Apps { realm_instance },
            pkgfs,
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
    commit_status_provider: CommitStatusProviderProxy,
    space_manager: SpaceManagerProxy,
    package_cache: PackageCacheProxy,
    retained_packages: RetainedPackagesProxy,
}

pub struct Mocks {
    pub logger_factory: Arc<MockLoggerFactory>,
    _paver_service: Arc<MockPaverService>,
    _verifier_service: Arc<MockVerifierService>,
}

struct Apps {
    realm_instance: RealmInstance,
}

struct TestEnv<P = PkgfsRamdisk> {
    apps: Apps,
    pkgfs: P,
    proxies: Proxies,
    pub mocks: Mocks,
}

impl TestEnv<PkgfsRamdisk> {
    // workaround for fxbug.dev/38162
    async fn stop(self) {
        // Tear down the environment in reverse order, ending with the storage.
        drop(self.proxies);
        drop(self.apps);
        self.pkgfs.stop().await.unwrap();
    }
}

impl TestEnv<PkgfsRamdisk> {
    fn builder(
    ) -> TestEnvBuilder<fn() -> BoxFuture<'static, PkgfsRamdisk>, BoxFuture<'static, PkgfsRamdisk>>
    {
        TestEnvBuilder::new()
    }

    fn blobfs(&self) -> &BlobfsRamdisk {
        self.pkgfs.blobfs()
    }
}

impl<P: PkgFs> TestEnv<P> {
    async fn inspect_hierarchy(&self) -> DiagnosticsHierarchy {
        let nested_environment_label = format!(
            "pkg_cache_integration_test/fuchsia_component_test_collection\\:{}",
            self.apps.realm_instance.root.child_name()
        );

        get_inspect_hierarchy(&nested_environment_label, "pkg_cache").await
    }

    pub async fn open_package(&self, merkle: &str) -> Result<DirectoryProxy, zx::Status> {
        let (package, server_end) = fidl::endpoints::create_proxy().unwrap();
        let status_fut = self.proxies.package_cache.open(
            &mut merkle.parse::<BlobId>().unwrap().into(),
            &mut vec![].into_iter(),
            server_end,
        );

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
                    .parse::<BlobId>()
                    .unwrap()
                    .into(),
                &mut vec![].into_iter(),
                server_end,
            )
            .await
            .unwrap();

        // Also, make sure the system-update-committer starts to prevent race conditions
        // where the system-update-commiter drops before the paver.
        let _ = self.proxies.commit_status_provider.is_current_system_committed().await.unwrap();
    }

    /// Wait until pkg-cache inspect state satisfies `desired_state`.
    pub async fn wait_for_inspect_state(&self, desired_state: TreeAssertion<String>) {
        while desired_state.run(&self.inspect_hierarchy().await).is_err() {
            fasync::Timer::new(Duration::from_millis(10)).await;
        }
    }

    pub fn client(&self) -> fidl_fuchsia_pkg_ext::cache::Client {
        fidl_fuchsia_pkg_ext::cache::Client::from_proxy(self.proxies.package_cache.clone())
    }
}

struct MockLogger {
    cobalt_events: Mutex<Vec<CobaltEvent>>,
}

impl MockLogger {
    fn new() -> Self {
        Self { cobalt_events: Mutex::new(vec![]) }
    }

    async fn run_logger(self: Arc<Self>, mut stream: fidl_fuchsia_cobalt::LoggerRequestStream) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl_fuchsia_cobalt::LoggerRequest::LogCobaltEvent { event, responder } => {
                    self.cobalt_events.lock().push(event);
                    let _ = responder.send(fidl_fuchsia_cobalt::Status::Ok);
                }
                _ => {
                    panic!("unhandled Logger method {:?}", event);
                }
            }
        }
    }
}

pub struct MockLoggerFactory {
    loggers: Mutex<Vec<Arc<MockLogger>>>,
}

impl MockLoggerFactory {
    fn new() -> Self {
        Self { loggers: Mutex::new(vec![]) }
    }

    async fn run_logger_factory(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_cobalt::LoggerFactoryRequestStream,
    ) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl_fuchsia_cobalt::LoggerFactoryRequest::CreateLoggerFromProjectId {
                    project_id,
                    logger,
                    responder,
                } => {
                    assert_eq!(project_id, cobalt_sw_delivery_registry::PROJECT_ID);
                    let mock_logger = Arc::new(MockLogger::new());
                    self.loggers.lock().push(mock_logger.clone());
                    fasync::Task::spawn(mock_logger.run_logger(logger.into_stream().unwrap()))
                        .detach();
                    let _ = responder.send(fidl_fuchsia_cobalt::Status::Ok);
                }
                _ => {
                    panic!("unhandled LoggerFactory method: {:?}", event);
                }
            }
        }
    }

    pub async fn wait_for_at_least_n_events_with_metric_id(
        &self,
        n: usize,
        id: u32,
    ) -> Vec<CobaltEvent> {
        loop {
            let events: Vec<CobaltEvent> = self
                .loggers
                .lock()
                .iter()
                .flat_map(|logger| logger.cobalt_events.lock().clone().into_iter())
                .filter(|CobaltEvent { metric_id, .. }| *metric_id == id)
                .collect();
            if events.len() >= n {
                return events;
            }
            fasync::Timer::new(Duration::from_millis(10)).await;
        }
    }
}

struct TempDirPkgFs {
    root: TempDir,
}

impl TempDirPkgFs {
    fn new() -> Self {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let fake_package_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![(
            PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
            fake_package_hash,
        )]);
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "some-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "other-blob".to_string() =>
                        "3333333333333333333333333333333333333333333333333333333333333333".parse().unwrap()
                }
            ).unwrap()
        };
        let root = tempfile::tempdir().unwrap();

        create_dir(root.path().join("ctl")).unwrap();

        create_dir(root.path().join("system")).unwrap();
        File::create(root.path().join("system/meta"))
            .unwrap()
            .write_all(system_image_hash.to_string().as_bytes())
            .unwrap();
        create_dir(root.path().join("system/data")).unwrap();
        static_packages
            .serialize(File::create(root.path().join("system/data/static_packages")).unwrap())
            .unwrap();

        create_dir(root.path().join("versions")).unwrap();
        for (hash, contents) in versions_contents.iter() {
            let meta_path = root.path().join(format!("versions/{}/meta", hash));
            create_dir_all(&meta_path).unwrap();
            contents.serialize(&mut File::create(meta_path.join("contents")).unwrap()).unwrap();
        }

        create_dir(root.path().join("blobfs")).unwrap();

        Self { root }
    }

    fn garbage_path(&self) -> PathBuf {
        self.root.path().join("ctl/do-not-use-this-garbage")
    }

    fn create_garbage(&self) {
        File::create(self.garbage_path()).unwrap();
    }

    pub fn emulate_ctl_error(&self) {
        remove_dir(self.root.path().join("ctl")).unwrap();
    }
}

impl PkgFs for TempDirPkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        Ok(fdio::transfer_fd(File::open(self.root.path()).unwrap()).unwrap().into())
    }

    fn blobfs_root_proxy(&self) -> Result<DirectoryProxy, Error> {
        let dir_handle: ClientEnd<DirectoryMarker> =
            fdio::transfer_fd(File::open(self.root.path().join("blobfs")).unwrap()).unwrap().into();
        Ok(dir_handle.into_proxy().unwrap())
    }
}
