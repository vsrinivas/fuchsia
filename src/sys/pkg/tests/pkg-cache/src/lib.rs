// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::{anyhow, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_cobalt::CobaltEvent,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_pkg::{PackageCacheMarker, PackageCacheProxy},
    fidl_fuchsia_pkg_ext::BlobId,
    fidl_fuchsia_space::{ManagerMarker as SpaceManagerMarker, ManagerProxy as SpaceManagerProxy},
    fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderProxy},
    fidl_test_pkg_reflector::ReflectorMarker,
    fuchsia_async as fasync,
    fuchsia_component::{client::ScopedInstance, server::ServiceFs},
    fuchsia_inspect::reader::DiagnosticsHierarchy,
    fuchsia_pkg_testing::get_inspect_hierarchy,
    fuchsia_zircon as zx,
    futures::prelude::*,
    mock_paver::{MockPaverService, MockPaverServiceBuilder},
    parking_lot::Mutex,
    pkgfs_ramdisk::PkgfsRamdisk,
    std::sync::Arc,
};

mod base_pkg_index;
mod cobalt;
mod inspect;
mod space;
mod sync;

const TEST_CASE_COLLECTION: &str = "pkg_cache_test_realm";
const TEST_CASE_REALM: &str =
    "fuchsia-pkg://fuchsia.com/pkg-cache-integration-tests#meta/test-case-realm.cm";

trait PkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;
}

impl PkgFs for PkgfsRamdisk {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        PkgfsRamdisk::root_dir_handle(self)
    }
}

struct TestEnvBuilder<PkgFsFn, P>
where
    PkgFsFn: FnOnce() -> P,
    P: PkgFs,
{
    paver_service_builder: Option<MockPaverServiceBuilder>,
    pkgfs: PkgFsFn,
}

impl TestEnvBuilder<fn() -> PkgfsRamdisk, PkgfsRamdisk> {
    fn new() -> Self {
        Self {
            pkgfs: || PkgfsRamdisk::start().expect("pkgfs to start"),
            paver_service_builder: None,
        }
    }
}

impl<PkgFsFn, P> TestEnvBuilder<PkgFsFn, P>
where
    PkgFsFn: FnOnce() -> P,
    P: PkgFs,
{
    fn paver_service_builder(self, paver_service_builder: MockPaverServiceBuilder) -> Self {
        Self { paver_service_builder: Some(paver_service_builder), ..self }
    }

    fn pkgfs<Pother>(self, pkgfs: Pother) -> TestEnvBuilder<impl FnOnce() -> Pother, Pother>
    where
        Pother: PkgFs + 'static,
    {
        TestEnvBuilder::<_, Pother> {
            pkgfs: || pkgfs,
            paver_service_builder: self.paver_service_builder,
        }
    }

    async fn build(self) -> TestEnv<P> {
        let mut fs = ServiceFs::new();

        let pkgfs = (self.pkgfs)();
        fs.add_remote("pkgfs", pkgfs.root_dir_handle().unwrap().into_proxy().unwrap());

        let logger_factory = Arc::new(MockLoggerFactory::new());
        let logger_factory_clone = Arc::clone(&logger_factory);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&logger_factory_clone).run_logger_factory(stream))
                .detach()
        });

        let paver_service = Arc::new(
            self.paver_service_builder.unwrap_or_else(|| MockPaverServiceBuilder::new()).build(),
        );
        let paver_service_clone = Arc::clone(&paver_service);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&paver_service_clone)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach()
        });

        let (reflected_dir_client_end, reflected_dir_server_end) =
            fidl::endpoints::create_endpoints::<DirectoryMarker>()
                .expect("creating reflected channel");

        fs.serve_connection(reflected_dir_server_end.into_channel()).unwrap();

        fasync::Task::spawn(fs.collect()).detach();

        let pkg_cache_realm =
            ScopedInstance::new(TEST_CASE_COLLECTION.to_string(), TEST_CASE_REALM.to_string())
                .await
                .expect("scoped instance to create successfully");

        let reflector = pkg_cache_realm
            .connect_to_protocol_at_exposed_dir::<ReflectorMarker>()
            .expect("connecting to reflector");

        reflector.reflect(reflected_dir_client_end).await.expect("reflect to work");

        let proxies = Proxies {
            commit_status_provider: pkg_cache_realm
                .connect_to_protocol_at_exposed_dir::<CommitStatusProviderMarker>()
                .expect("connect to commit status provider"),
            space_manager: pkg_cache_realm
                .connect_to_protocol_at_exposed_dir::<SpaceManagerMarker>()
                .expect("connect to space manager"),
            package_cache: pkg_cache_realm
                .connect_to_protocol_at_exposed_dir::<PackageCacheMarker>()
                .expect("connect to package cache"),
        };

        TestEnv {
            apps: Apps { pkg_cache: pkg_cache_realm },
            pkgfs,
            proxies,
            mocks: Mocks { logger_factory, _paver_service: paver_service },
        }
    }
}

struct Proxies {
    commit_status_provider: CommitStatusProviderProxy,
    space_manager: SpaceManagerProxy,
    package_cache: PackageCacheProxy,
}

pub struct Mocks {
    pub logger_factory: Arc<MockLoggerFactory>,
    _paver_service: Arc<MockPaverService>,
}

struct Apps {
    pkg_cache: ScopedInstance,
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
            fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(10))).await;
        }
    }
}

impl TestEnv<PkgfsRamdisk> {
    fn builder() -> TestEnvBuilder<fn() -> PkgfsRamdisk, PkgfsRamdisk> {
        TestEnvBuilder::new()
    }
}

impl<P: PkgFs> TestEnv<P> {
    async fn inspect_hierarchy(&self) -> DiagnosticsHierarchy {
        let nested_environment_label = format!(
            "pkg_cache_integration_test/pkg_cache_test_realm\\:{}",
            self.apps.pkg_cache.child_name()
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
}
