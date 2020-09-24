// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_cobalt::CobaltEvent,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_pkg::{PackageCacheMarker, PackageCacheProxy},
    fidl_fuchsia_pkg_ext::BlobId,
    fidl_fuchsia_space::{ManagerMarker as SpaceManagerMarker, ManagerProxy as SpaceManagerProxy},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_inspect::reader::NodeHierarchy,
    fuchsia_pkg_testing::get_inspect_hierarchy,
    fuchsia_zircon as zx,
    futures::prelude::*,
    parking_lot::Mutex,
    pkgfs_ramdisk::PkgfsRamdisk,
    std::sync::Arc,
};

mod base_pkg_index;
mod cobalt;
mod inspect;
mod space;
mod sync;

trait PkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;
}

impl PkgFs for PkgfsRamdisk {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        PkgfsRamdisk::root_dir_handle(self)
    }
}

struct Proxies {
    space_manager: SpaceManagerProxy,
    package_cache: PackageCacheProxy,
}

pub struct Mocks {
    pub logger_factory: Arc<MockLoggerFactory>,
}

struct TestEnv<P = PkgfsRamdisk> {
    _env: NestedEnvironment,
    pkgfs: P,
    proxies: Proxies,
    pkg_cache: App,
    nested_environment_label: String,
    pub mocks: Mocks,
}

impl TestEnv<PkgfsRamdisk> {
    // workaround for fxbug.dev/38162
    async fn stop(self) {
        // Tear down the environment in reverse order, ending with the storage.
        drop(self.proxies);
        drop(self.pkg_cache);
        drop(self._env);
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

impl<P: PkgFs> TestEnv<P> {
    fn new(pkgfs: P) -> Self {
        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>()
            .add_proxy_service::<fidl_fuchsia_tracing_provider::RegistryMarker, _>();

        let logger_factory = Arc::new(MockLoggerFactory::new());
        let logger_factory_clone = Arc::clone(&logger_factory);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&logger_factory_clone).run_logger_factory(stream))
                .detach()
        });

        let pkg_cache = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-cache-integration-tests#meta/pkg-cache-without-pkgfs.cmx".to_string(),
        )
            .add_handle_to_namespace("/pkgfs".to_owned(), pkgfs.root_dir_handle().unwrap().into());

        let nested_environment_label = Self::make_nested_environment_label();
        let env = fs
            .create_nested_environment(&nested_environment_label)
            .expect("nested environment to create successfully");

        fasync::Task::spawn(fs.collect()).detach();

        let pkg_cache = pkg_cache.spawn(env.launcher()).expect("pkg_cache to launch");

        let proxies = Proxies {
            space_manager: pkg_cache
                .connect_to_service::<SpaceManagerMarker>()
                .expect("connect to space manager"),
            package_cache: pkg_cache.connect_to_service::<PackageCacheMarker>().unwrap(),
        };

        Self {
            _env: env,
            pkgfs,
            proxies,
            pkg_cache,
            nested_environment_label,
            mocks: Mocks { logger_factory },
        }
    }

    async fn inspect_hierarchy(&self) -> NodeHierarchy {
        get_inspect_hierarchy(&self.nested_environment_label, "pkg-cache-without-pkgfs.cmx").await
    }

    fn make_nested_environment_label() -> String {
        let mut salt = [0; 4];
        zx::cprng_draw(&mut salt[..]).expect("zx_cprng_draw does not fail");
        format!("pkg-cache-env_{}", hex::encode(&salt))
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
    }
}
