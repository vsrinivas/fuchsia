// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::let_unit_value)]
#![cfg(test)]

use {
    self::SystemUpdaterInteraction::*,
    anyhow::{anyhow, Context as _, Error},
    assert_matches::assert_matches,
    cobalt_sw_delivery_registry as metrics, fidl_fuchsia_io as fio, fidl_fuchsia_paver as paver,
    fidl_fuchsia_pkg::{BlobIdIteratorProxy, PackageResolverRequestStream},
    fidl_fuchsia_update_installer::{InstallerMarker, InstallerProxy},
    fidl_fuchsia_update_installer_ext::{
        start_update, Initiator, Options, UpdateAttempt, UpdateAttemptError,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_hash::Hash,
    fuchsia_pkg_testing::{
        make_current_epoch_json, make_epoch_json, make_packages_json, SOURCE_EPOCH,
    },
    fuchsia_url::AbsoluteComponentUrl,
    fuchsia_zircon::Status,
    futures::prelude::*,
    mock_metrics::MockMetricEventLoggerFactory,
    mock_paver::{hooks as mphooks, MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_reboot::{MockRebootService, RebootReason},
    mock_resolver::MockResolverService,
    parking_lot::Mutex,
    pretty_assertions::assert_eq,
    serde_json::json,
    std::{
        collections::HashSet,
        fs::{create_dir, File},
        io::Write,
        path::PathBuf,
        str::FromStr,
        sync::Arc,
    },
    tempfile::TempDir,
};

mod board;
mod cobalt_metrics;
mod commits_images;
mod epoch;
mod fetch_packages;
mod history;
mod mode_force_recovery;
mod mode_normal;
mod progress_reporting;
mod reboot_controller;
mod retained_packages;
mod update_package;
mod writes_firmware;
mod writes_images;

const EMPTY_HASH: &str = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
const MATCHING_HASH: &str = "e0705e68b0468289858b543f8a57f375a3b4f46391a72f94a28d82d6a3dacaa7";

pub fn make_images_json_zbi() -> String {
    serde_json::to_string(
        &::update_package::ImagePackagesManifest::builder()
            .fuchsia_package(
                ::update_package::ImageMetadata::new(
                    0,
                    Hash::from_str(EMPTY_HASH).unwrap(),
                    image_package_resource_url("update-images-fuchsia", 9, "zbi"),
                ),
                None,
            )
            .clone()
            .build(),
    )
    .unwrap()
}

pub fn make_images_json_recovery() -> String {
    serde_json::to_string(
        &::update_package::ImagePackagesManifest::builder()
            .recovery_package(
                ::update_package::ImageMetadata::new(
                    0,
                    Hash::from_str(EMPTY_HASH).unwrap(),
                    image_package_resource_url("update-images-recovery", 9, "zbi"),
                ),
                None,
            )
            .clone()
            .build(),
    )
    .unwrap()
}

// A set of tags for interactions the system updater has with external services.
// We aren't tracking Cobalt interactions, since those may arrive out of order,
// and they are tested in individual tests which care about them specifically.
#[derive(Debug, PartialEq, Clone)]
enum SystemUpdaterInteraction {
    BlobfsSync,
    Gc,
    PackageResolve(String),
    Paver(PaverEvent),
    Reboot,
    ClearRetainedPackages,
    ReplaceRetainedPackages(Vec<fidl_fuchsia_pkg_ext::BlobId>),
}

#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy)]
enum Protocol {
    PackageResolver,
    PackageCache,
    SpaceManager,
    Paver,
    Reboot,
    FuchsiaMetrics,
    RetainedPackages,
}

type SystemUpdaterInteractions = Arc<Mutex<Vec<SystemUpdaterInteraction>>>;

struct TestEnvBuilder {
    paver_service_builder: MockPaverServiceBuilder,
    blocked_protocols: HashSet<Protocol>,
    mount_data: bool,
    history: Option<serde_json::Value>,
    system_image_hash: Option<fuchsia_hash::Hash>,
}

impl TestEnvBuilder {
    fn new() -> Self {
        TestEnvBuilder {
            paver_service_builder: MockPaverServiceBuilder::new(),
            blocked_protocols: HashSet::new(),
            mount_data: true,
            history: None,
            system_image_hash: None,
        }
    }

    fn paver_service<F>(mut self, f: F) -> Self
    where
        F: FnOnce(MockPaverServiceBuilder) -> MockPaverServiceBuilder,
    {
        self.paver_service_builder = f(self.paver_service_builder);
        self
    }

    fn unregister_protocol(mut self, protocol: Protocol) -> Self {
        self.blocked_protocols.insert(protocol);
        self
    }

    fn mount_data(mut self, mount_data: bool) -> Self {
        self.mount_data = mount_data;
        self
    }

    fn history(mut self, history: serde_json::Value) -> Self {
        self.history = Some(history);
        self
    }

    fn system_image_hash(mut self, system_image: fuchsia_hash::Hash) -> Self {
        assert_eq!(self.system_image_hash, None);
        self.system_image_hash = Some(system_image);
        self
    }

    async fn build(self) -> TestEnv {
        let Self {
            paver_service_builder,
            blocked_protocols,
            mount_data,
            history,
            system_image_hash,
        } = self;

        let test_dir = TempDir::new().expect("create test tempdir");

        let data_path = test_dir.path().join("data");
        create_dir(&data_path).expect("create data dir");

        let build_info_path = test_dir.path().join("build-info");
        create_dir(&build_info_path).expect("create build-info dir");

        // Optionally write the pre-configured update history.
        if let Some(history) = history {
            serde_json::to_writer(
                File::create(data_path.join("update_history.json")).unwrap(),
                &history,
            )
            .unwrap()
        }

        let mut fs = ServiceFs::new();
        let data = fuchsia_fs::directory::open_in_namespace(
            data_path.to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let build_info = fuchsia_fs::directory::open_in_namespace(
            build_info_path.to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .unwrap();

        fs.add_remote("data", data);
        fs.dir("config").add_remote("build-info", build_info);

        if let Some(hash) = system_image_hash {
            let system_image_path = test_dir.path().join("pkgfs-system");
            create_dir(&system_image_path).expect("crate system-image dir");
            let mut meta = File::create(system_image_path.join("meta")).unwrap();
            let () = meta.write_all(hash.to_string().as_bytes()).unwrap();
            let pkgfs_system = fuchsia_fs::directory::open_in_namespace(
                system_image_path.to_str().unwrap(),
                fuchsia_fs::OpenFlags::RIGHT_READABLE,
            )
            .unwrap();
            fs.add_remote("pkgfs-system", pkgfs_system);
        }

        // A buffer to store all the interactions the system-updater has with external services.
        let interactions = Arc::new(Mutex::new(vec![]));
        let interactions_paver_clone = Arc::clone(&interactions);
        let paver_service = Arc::new(
            paver_service_builder
                .event_hook(move |event| {
                    interactions_paver_clone.lock().push(Paver(event.clone()));
                })
                .build(),
        );

        let resolver = {
            let interactions = Arc::clone(&interactions);
            Arc::new(MockResolverService::new(Some(Box::new(move |resolved_url: &str| {
                interactions.lock().push(PackageResolve(resolved_url.to_owned()))
            }))))
        };

        let reboot_service = {
            let interactions = Arc::clone(&interactions);
            Arc::new(MockRebootService::new(Box::new(move |reason| {
                assert_eq!(reason, RebootReason::SystemUpdate);
                interactions.lock().push(Reboot);
                Ok(())
            })))
        };

        let cache_service = Arc::new(MockCacheService::new(Arc::clone(&interactions)));
        let logger_factory = Arc::new(MockMetricEventLoggerFactory::new());
        let space_service = Arc::new(MockSpaceService::new(Arc::clone(&interactions)));
        let retained_packages_service =
            Arc::new(MockRetainedPackagesService::new(Arc::clone(&interactions)));

        // Register the mock services with the test environment service provider.
        {
            let resolver = Arc::clone(&resolver);
            let paver_service = Arc::clone(&paver_service);
            let reboot_service = Arc::clone(&reboot_service);
            let cache_service = Arc::clone(&cache_service);
            let logger_factory = Arc::clone(&logger_factory);
            let space_service = Arc::clone(&space_service);
            let retained_packages_service = Arc::clone(&retained_packages_service);

            let should_register = |protocol: Protocol| !blocked_protocols.contains(&protocol);

            if should_register(Protocol::PackageResolver) {
                fs.dir("svc").add_fidl_service(move |stream: PackageResolverRequestStream| {
                    fasync::Task::spawn(
                        Arc::clone(&resolver)
                            .run_resolver_service(stream)
                            .unwrap_or_else(|e| panic!("error running resolver service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::Paver) {
                fs.dir("svc").add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&paver_service)
                            .run_paver_service(stream)
                            .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::Reboot) {
                fs.dir("svc").add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&reboot_service)
                            .run_reboot_service(stream)
                            .unwrap_or_else(|e| panic!("error running reboot service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::PackageCache) {
                fs.dir("svc").add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&cache_service)
                            .run_cache_service(stream)
                            .unwrap_or_else(|e| panic!("error running cache service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::FuchsiaMetrics) {
                fs.dir("svc").add_fidl_service(move |stream| {
                    fasync::Task::spawn(Arc::clone(&logger_factory).run_logger_factory(stream))
                        .detach()
                });
            }
            if should_register(Protocol::SpaceManager) {
                fs.dir("svc").add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&space_service)
                            .run_space_service(stream)
                            .unwrap_or_else(|e| panic!("error running space service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::RetainedPackages) {
                fs.dir("svc").add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&retained_packages_service)
                            .run_retained_packages_service(stream)
                            .unwrap_or_else(|e| {
                                panic!("error running retained packages service: {:?}", e)
                            }),
                    )
                    .detach()
                });
            }
        }

        let fs_holder = Mutex::new(Some(fs));
        let builder = RealmBuilder::new().await.expect("Failed to create test realm builder");
        let system_updater = builder
            .add_child(
                "system_updater",
                "fuchsia-pkg://fuchsia.com/system-updater-integration-tests#meta/system-updater-isolated.cm",
                ChildOptions::new().eager(),
            ).await.unwrap();
        let fake_capabilities = builder
            .add_local_child(
                "fake_capabilities",
                move |mock_handles| {
                    let mut rfs = fs_holder
                        .lock()
                        .take()
                        .expect("mock component should only be launched once");
                    async {
                        let _ = &mock_handles;
                        rfs.serve_connection(mock_handles.outgoing_dir).unwrap();
                        let () = rfs.collect().await;
                        Ok(())
                    }
                    .boxed()
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
                    .to(&system_updater),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.update.installer.Installer"))
                    .from(&system_updater)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.metrics.MetricEventLoggerFactory",
                    ))
                    .capability(Capability::protocol_by_name("fuchsia.paver.Paver"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageCache"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageResolver"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.RetainedPackages"))
                    .capability(Capability::protocol_by_name("fuchsia.space.Manager"))
                    .capability(Capability::protocol_by_name(
                        "fuchsia.hardware.power.statecontrol.Admin",
                    ))
                    .capability(
                        Capability::directory("build-info")
                            .path("/config/build-info")
                            .rights(fio::R_STAR_DIR),
                    )
                    .from(&fake_capabilities)
                    .to(&system_updater),
            )
            .await
            .unwrap();

        if mount_data {
            builder
                .add_route(
                    Route::new()
                        .capability(
                            Capability::directory("data").path("/data").rights(fio::RW_STAR_DIR),
                        )
                        .from(&fake_capabilities)
                        .to(&system_updater),
                )
                .await
                .unwrap();
        }

        if system_image_hash.is_some() {
            builder
                .add_route(
                    Route::new()
                        .capability(
                            Capability::directory("pkgfs-system")
                                .path("/pkgfs-system")
                                .rights(fio::R_STAR_DIR),
                        )
                        .from(&fake_capabilities)
                        .to(&system_updater),
                )
                .await
                .unwrap();
        }

        let realm_instance = builder.build().await.unwrap();

        TestEnv {
            realm_instance,
            resolver,
            _paver_service: paver_service,
            _reboot_service: reboot_service,
            cache_service,
            metric_event_logger_factory: logger_factory,
            _space_service: space_service,
            _test_dir: test_dir,
            data_path,
            build_info_path,
            interactions,
        }
    }
}

struct TestEnv {
    realm_instance: RealmInstance,
    resolver: Arc<MockResolverService>,
    _paver_service: Arc<MockPaverService>,
    _reboot_service: Arc<MockRebootService>,
    cache_service: Arc<MockCacheService>,
    metric_event_logger_factory: Arc<MockMetricEventLoggerFactory>,
    _space_service: Arc<MockSpaceService>,
    _test_dir: TempDir,
    data_path: PathBuf,
    build_info_path: PathBuf,
    interactions: SystemUpdaterInteractions,
}

impl TestEnv {
    async fn new() -> Self {
        Self::builder().build().await
    }

    fn builder() -> TestEnvBuilder {
        TestEnvBuilder::new()
    }

    fn take_interactions(&self) -> Vec<SystemUpdaterInteraction> {
        std::mem::take(&mut *self.interactions.lock())
    }

    /// Set the name of the board that system-updater is running on.
    fn set_board_name(&self, board: impl AsRef<str>) {
        // Write the "board" file into the build-info directory.
        let mut file = File::create(self.build_info_path.join("board")).expect("create board file");
        file.write_all(board.as_ref().as_bytes()).expect("write board file");
    }

    /// Set the version of the build that system-updater is running on.
    fn set_build_version(&self, version: impl AsRef<str>) {
        // Write the "version" file into the build-info directory.
        let mut file =
            File::create(self.build_info_path.join("version")).expect("create version file");
        file.write_all(version.as_ref().as_bytes()).expect("write version file");
    }

    fn read_history(&self) -> Option<serde_json::Value> {
        match File::open(self.data_path.join("update_history.json")) {
            Ok(f) => Some(serde_json::from_reader(f).unwrap()),
            Err(e) => {
                assert_eq!(e.kind(), std::io::ErrorKind::NotFound, "io error: {:?}", e);
                None
            }
        }
    }

    fn write_history(&self, history: serde_json::Value) {
        serde_json::to_writer(
            File::create(self.data_path.join("update_history.json")).unwrap(),
            &history,
        )
        .unwrap()
    }

    async fn start_update(&self) -> Result<UpdateAttempt, UpdateAttemptError> {
        self.start_update_with_options(UPDATE_PKG_URL, default_options()).await
    }

    async fn start_update_with_options(
        &self,
        url: &str,
        options: Options,
    ) -> Result<UpdateAttempt, UpdateAttemptError> {
        start_update(&url.parse().unwrap(), options, &self.installer_proxy(), None).await
    }

    async fn run_update(&self) -> Result<(), Error> {
        self.run_update_with_options(UPDATE_PKG_URL, default_options()).await
    }

    async fn run_update_with_options(&self, url: &str, options: Options) -> Result<(), Error> {
        let mut update_attempt = self.start_update_with_options(url, options).await?;

        while let Some(state) =
            update_attempt.try_next().await.context("fetching next update state")?
        {
            if state.is_success() {
                // Wait until the stream terminates before returning so that interactions will
                // include reboot.
                assert_matches!(update_attempt.try_next().await, Ok(None));
                return Ok(());
            } else if state.is_failure() {
                // Wait until the stream terminates before returning so that any subsequent
                // attempts won't get already in progress error.
                assert_matches!(update_attempt.try_next().await, Ok(None));
                return Err(anyhow!("update attempt failed"));
            }
        }

        Err(anyhow!("unexpected end of update attempt"))
    }

    /// Opens a connection to the installer fidl service.
    fn installer_proxy(&self) -> InstallerProxy {
        self.realm_instance.root.connect_to_protocol_at_exposed_dir::<InstallerMarker>().unwrap()
    }

    async fn get_ota_metrics(&self) -> OtaMetrics {
        let loggers = self.metric_event_logger_factory.clone_loggers();
        assert_eq!(loggers.len(), 1);
        let logger = loggers.into_iter().next().unwrap();
        let events = logger.clone_metric_events();
        OtaMetrics::from_events(events)
    }
}

struct MockCacheService {
    sync_response: Mutex<Option<Result<(), Status>>>,
    interactions: SystemUpdaterInteractions,
}
impl MockCacheService {
    fn new(interactions: SystemUpdaterInteractions) -> Self {
        Self { sync_response: Mutex::new(None), interactions }
    }

    fn set_sync_response(&self, response: Result<(), Status>) {
        self.sync_response.lock().replace(response);
    }

    async fn run_cache_service(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_pkg::PackageCacheRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await.expect("received request") {
            match event {
                fidl_fuchsia_pkg::PackageCacheRequest::Sync { responder } => {
                    self.interactions.lock().push(BlobfsSync);
                    responder.send(
                        &mut self.sync_response.lock().unwrap_or(Ok(())).map_err(|s| s.into_raw()),
                    )?;
                }
                other => panic!("unsupported PackageCache request: {:?}", other),
            }
        }

        Ok(())
    }
}

struct MockSpaceService {
    interactions: SystemUpdaterInteractions,
}
impl MockSpaceService {
    fn new(interactions: SystemUpdaterInteractions) -> Self {
        Self { interactions }
    }

    async fn run_space_service(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_space::ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await.expect("received request") {
            let fidl_fuchsia_space::ManagerRequest::Gc { responder } = event;
            self.interactions.lock().push(Gc);
            responder.send(&mut Ok(()))?;
        }

        Ok(())
    }
}

struct MockRetainedPackagesService {
    interactions: SystemUpdaterInteractions,
}
impl MockRetainedPackagesService {
    fn new(interactions: SystemUpdaterInteractions) -> Self {
        Self { interactions }
    }

    async fn run_retained_packages_service(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_pkg::RetainedPackagesRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await.expect("received request") {
            match event {
                fidl_fuchsia_pkg::RetainedPackagesRequest::Clear { responder } => {
                    self.interactions.lock().push(ClearRetainedPackages);
                    responder.send().unwrap();
                }
                fidl_fuchsia_pkg::RetainedPackagesRequest::Replace { iterator, responder } => {
                    let blobs =
                        Self::collect_blob_id_iterator(iterator.into_proxy().unwrap()).await;
                    self.interactions.lock().push(ReplaceRetainedPackages(blobs));
                    responder.send().unwrap();
                }
            }
        }

        Ok(())
    }

    async fn collect_blob_id_iterator(
        iterator: BlobIdIteratorProxy,
    ) -> Vec<fidl_fuchsia_pkg_ext::BlobId> {
        let mut blobs = vec![];
        loop {
            let new_blobs = iterator.next().await.unwrap();
            if new_blobs.is_empty() {
                break;
            }
            blobs.extend(new_blobs.into_iter().map(fidl_fuchsia_pkg_ext::BlobId::from));
        }
        blobs
    }
}

#[derive(PartialEq, Eq, Debug)]
struct OtaMetrics {
    initiator: u32,
    phase: u32,
    status_code: u32,
}

impl OtaMetrics {
    fn from_events(mut events: Vec<fidl_fuchsia_metrics::MetricEvent>) -> Self {
        events.sort_by_key(|e| e.metric_id);

        // expecting one of each event
        assert_eq!(
            events.iter().map(|e| e.metric_id).collect::<Vec<_>>(),
            vec![
                metrics::OTA_START_MIGRATED_METRIC_ID,
                metrics::OTA_RESULT_ATTEMPTS_MIGRATED_METRIC_ID,
                metrics::OTA_RESULT_DURATION_MIGRATED_METRIC_ID,
            ]
        );

        // we just asserted that we have the exact 4 things we're expecting, so unwrap them
        let mut iter = events.into_iter();
        let start = iter.next().unwrap();
        let attempt = iter.next().unwrap();
        let duration = iter.next().unwrap();

        // Some basic sanity checks follow
        assert_eq!(attempt.payload, fidl_fuchsia_metrics::MetricEventPayload::Count(1));

        let fidl_fuchsia_metrics::MetricEvent { event_codes, .. } = attempt;

        // metric event_codes and component should line up across all 3 result metrics
        assert_eq!(&duration.event_codes, &event_codes);

        // OtaStart only has initiator and hour_of_day, so just check initiator.
        assert_eq!(start.event_codes[0], event_codes[0]);

        assert_eq!(event_codes.len(), 3);
        let initiator = event_codes[0];
        let phase = event_codes[1];
        let status_code = event_codes[2];

        match duration.payload {
            fidl_fuchsia_metrics::MetricEventPayload::IntegerValue(_time) => {
                // Ignore the value since timing is not predictable.
            }
            other => {
                panic!("unexpected duration payload {:?}", other);
            }
        }

        Self { initiator, phase, status_code }
    }
}

#[macro_export]
macro_rules! merkle_str {
    ($seed:literal) => {{
        $crate::merkle_str!(@check $seed);
        $crate::merkle_str!(@unchecked $seed)
    }};
    (@check $seed:literal) => {
        assert_eq!($seed.len(), 2)
    };
    (@unchecked $seed:literal) => {
        concat!(
            $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed,
            $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed,
            $seed, $seed, $seed, $seed, $seed, $seed, $seed, $seed,
        )
    };
}

#[macro_export]
macro_rules! pinned_pkg_url {
    ($path:literal, $merkle_seed:literal) => {{
        $crate::merkle_str!(@check $merkle_seed);
        concat!("fuchsia-pkg://fuchsia.com/", $path, "?hash=", $crate::merkle_str!(@unchecked $merkle_seed))
    }};
}

const UPDATE_HASH: &str = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";
const SYSTEM_IMAGE_HASH: &str = "42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296";
const SYSTEM_IMAGE_URL: &str = "fuchsia-pkg://fuchsia.com/system_image/0?hash=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296";
const UPDATE_PKG_URL: &str = "fuchsia-pkg://fuchsia.com/update";
const UPDATE_PKG_URL_PINNED: &str = "fuchsia-pkg://fuchsia.com/update?hash=00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

fn resolved_urls(interactions: SystemUpdaterInteractions) -> Vec<String> {
    (*interactions.lock())
        .iter()
        .filter_map(|interaction| match interaction {
            PackageResolve(package_url) => Some(package_url.clone()),
            _ => None,
        })
        .collect()
}

fn default_options() -> Options {
    Options {
        initiator: Initiator::User,
        allow_attach_to_existing_attempt: true,
        should_write_recovery: true,
    }
}

fn force_recovery_json() -> String {
    json!({
      "version": "1",
      "content": {
        "mode": "force-recovery",
      }
    })
    .to_string()
}

fn image_package_resource_url(name: &str, hash: u8, resource: &str) -> AbsoluteComponentUrl {
    format!("fuchsia-pkg://fuchsia.com/{name}/0?hash={}#{resource}", hashstr(hash)).parse().unwrap()
}

fn image_package_url_to_string(name: &str, hash: u8) -> String {
    format!("fuchsia-pkg://fuchsia.com/{name}/0?hash={}", hashstr(hash)).parse().unwrap()
}

fn hash(n: u8) -> Hash {
    Hash::from([n; 32])
}

fn hashstr(n: u8) -> String {
    hash(n).to_string()
}
