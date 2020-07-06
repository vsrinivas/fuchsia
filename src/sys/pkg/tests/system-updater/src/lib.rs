// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    self::SystemUpdaterInteraction::{BlobfsSync, Gc, PackageResolve, Paver, Reboot},
    anyhow::Error,
    cobalt_sw_delivery_registry as metrics, fidl_fuchsia_paver as paver,
    fidl_fuchsia_pkg::PackageResolverRequestStream,
    fidl_fuchsia_sys::{LauncherProxy, TerminationReason},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::AppBuilder,
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::prelude::*,
    mock_paver::{MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_resolver::MockResolverService,
    parking_lot::Mutex,
    pretty_assertions::assert_eq,
    serde_json::json,
    std::{
        fs::{self, create_dir, File},
        io::Write,
        path::PathBuf,
        sync::Arc,
    },
    tempfile::TempDir,
};

mod board;
mod channel;
mod cobalt_metrics;
mod fetch_packages;
mod mode_force_recovery;
mod mode_normal;
mod options;
mod update_package;
mod writes_firmware;
mod writes_images;

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
}

type SystemUpdaterInteractions = Arc<Mutex<Vec<SystemUpdaterInteraction>>>;

struct TestEnvBuilder {
    paver_service_builder: MockPaverServiceBuilder,
}

impl TestEnvBuilder {
    fn new() -> Self {
        TestEnvBuilder { paver_service_builder: MockPaverServiceBuilder::new() }
    }

    fn paver_service<F>(mut self, f: F) -> Self
    where
        F: FnOnce(MockPaverServiceBuilder) -> MockPaverServiceBuilder,
    {
        self.paver_service_builder = f(self.paver_service_builder);
        self
    }

    fn build(self) -> TestEnv {
        // A buffer to store all the interactions the system-updater has with external services.
        let interactions = Arc::new(Mutex::new(vec![]));

        let test_dir = TempDir::new().expect("create test tempdir");

        let blobfs_path = test_dir.path().join("blob");
        create_dir(&blobfs_path).expect("create blob dir");

        let fake_path = test_dir.path().join("fake");
        create_dir(&fake_path).expect("create fake stimulus dir");

        let build_info_path = test_dir.path().join("build-info");
        create_dir(&build_info_path).expect("create build-info dir");

        let misc_path = test_dir.path().join("misc");
        create_dir(&misc_path).expect("create misc dir");

        // Set up the paver service to push events to our interactions buffer by overriding
        // call_hook and firmware_hook.
        let interactions_paver_clone = Arc::clone(&interactions);
        let paver_service = Arc::new(
            self.paver_service_builder
                .event_hook(move |event| {
                    interactions_paver_clone.lock().push(Paver(event.clone()));
                })
                .build(),
        );

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();

        let resolver = {
            let interactions = Arc::clone(&interactions);
            Arc::new(MockResolverService::new(Some(Box::new(move |resolved_url: &str| {
                interactions.lock().push(PackageResolve(resolved_url.to_owned()))
            }))))
        };
        let reboot_service = Arc::new(MockRebootService::new(Arc::clone(&interactions)));
        let cache_service = Arc::new(MockCacheService::new(Arc::clone(&interactions)));
        let logger_factory = Arc::new(MockLoggerFactory::new());
        let space_service = Arc::new(MockSpaceService::new(Arc::clone(&interactions)));

        // Register the mock services with the test environment service provider.
        {
            let resolver = Arc::clone(&resolver);
            let paver_service = Arc::clone(&paver_service);
            let reboot_service = Arc::clone(&reboot_service);
            let cache_service = Arc::clone(&cache_service);
            let logger_factory = Arc::clone(&logger_factory);
            let space_service = Arc::clone(&space_service);

            fs.add_fidl_service(move |stream: PackageResolverRequestStream| {
                fasync::spawn(
                    Arc::clone(&resolver)
                        .run_resolver_service(stream)
                        .unwrap_or_else(|e| panic!("error running resolver service: {:?}", e)),
                )
            })
            .add_fidl_service(move |stream| {
                fasync::spawn(
                    Arc::clone(&paver_service)
                        .run_paver_service(stream)
                        .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                )
            })
            .add_fidl_service(move |stream| {
                fasync::spawn(
                    Arc::clone(&reboot_service)
                        .run_reboot_service(stream)
                        .unwrap_or_else(|e| panic!("error running reboot service: {:?}", e)),
                )
            })
            .add_fidl_service(move |stream| {
                fasync::spawn(
                    Arc::clone(&cache_service)
                        .run_cache_service(stream)
                        .unwrap_or_else(|e| panic!("error running cache service: {:?}", e)),
                )
            })
            .add_fidl_service(move |stream| {
                fasync::spawn(
                    Arc::clone(&logger_factory)
                        .run_logger_factory(stream)
                        .unwrap_or_else(|e| panic!("error running logger factory: {:?}", e)),
                )
            })
            .add_fidl_service(move |stream| {
                fasync::spawn(
                    Arc::clone(&space_service)
                        .run_space_service(stream)
                        .unwrap_or_else(|e| panic!("error running space service: {:?}", e)),
                )
            });
        }

        let env = fs
            .create_salted_nested_environment("systemupdater_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        TestEnv {
            env,
            resolver,
            _paver_service: paver_service,
            _reboot_service: reboot_service,
            cache_service,
            logger_factory,
            _space_service: space_service,
            _test_dir: test_dir,
            blobfs_path,
            fake_path,
            build_info_path,
            misc_path,
            interactions,
        }
    }
}

struct TestEnv {
    env: NestedEnvironment,
    resolver: Arc<MockResolverService>,
    _paver_service: Arc<MockPaverService>,
    _reboot_service: Arc<MockRebootService>,
    cache_service: Arc<MockCacheService>,
    logger_factory: Arc<MockLoggerFactory>,
    _space_service: Arc<MockSpaceService>,
    _test_dir: TempDir,
    blobfs_path: PathBuf,
    fake_path: PathBuf,
    build_info_path: PathBuf,
    misc_path: PathBuf,
    interactions: SystemUpdaterInteractions,
}

impl TestEnv {
    fn new() -> Self {
        Self::builder().build()
    }

    fn builder() -> TestEnvBuilder {
        TestEnvBuilder::new()
    }

    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    fn take_interactions(&self) -> Vec<SystemUpdaterInteraction> {
        std::mem::replace(&mut *self.interactions.lock(), vec![])
    }

    /// Set the name of the board that system-updater is running on.
    fn set_board_name(&self, board: impl AsRef<str>) {
        // Write the "board" file into the build-info directory.
        let mut file = File::create(self.build_info_path.join("board")).expect("create board file");
        file.write_all(board.as_ref().as_bytes()).expect("write board file");
    }

    fn set_target_channel(&self, contents: impl AsRef<[u8]>) {
        let misc_ota_dir = self.misc_path.join("ota");
        fs::create_dir_all(&misc_ota_dir).unwrap();

        fs::write(misc_ota_dir.join("target_channel.json"), contents.as_ref()).unwrap();
    }

    fn verify_current_channel(&self, expected: Option<&[u8]>) {
        match fs::read(self.misc_path.join("ota/current_channel.json")) {
            Ok(bytes) => assert_eq!(bytes, expected.unwrap()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => assert_eq!(expected, None),
            Err(e) => panic!(e),
        }
    }

    async fn run_system_updater<'a>(
        &'a self,
        args: SystemUpdaterArgs<'a>,
    ) -> Result<(), fuchsia_component::client::OutputError> {
        let mut v = vec![
            "--initiator".to_string(),
            format!("{}", args.initiator),
            "--target".to_string(),
            format!("{}", args.target),
        ];

        if let Some(update) = args.update {
            v.append(&mut vec!["--update".to_string(), format!("{}", update)]);
        }
        if let Some(reboot) = args.reboot {
            v.append(&mut vec!["--reboot".to_string(), format!("{}", reboot)]);
        }
        if let Some(skip_recovery) = args.skip_recovery {
            v.append(&mut vec!["--skip-recovery".to_string(), format!("{}", skip_recovery)]);
        }

        self.run_system_updater_args(v).await
    }

    async fn run_system_updater_args<'a>(
        &'a self,
        args: impl IntoIterator<Item = impl Into<String>>,
    ) -> Result<(), fuchsia_component::client::OutputError> {
        let launcher = self.launcher();
        let blobfs_dir = File::open(&self.blobfs_path).expect("open blob dir");
        let fake_dir = File::open(&self.fake_path).expect("open fake stimulus dir");
        let build_info_dir = File::open(&self.build_info_path).expect("open config dir");
        let misc_dir = File::open(&self.misc_path).expect("open misc dir");

        let system_updater = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/system-updater-integration-tests#meta/system-updater.cmx",
        )
        .add_dir_to_namespace("/blob".to_string(), blobfs_dir)
        .expect("/blob to mount")
        .add_dir_to_namespace("/fake".to_string(), fake_dir)
        .expect("/fake to mount")
        .add_dir_to_namespace("/config/build-info".to_string(), build_info_dir)
        .expect("/config/build-info to mount")
        .add_dir_to_namespace("/misc".to_string(), misc_dir)
        .expect("/misc to mount")
        .args(args);

        let output = system_updater
            .output(launcher)
            .expect("system updater to launch")
            .await
            .expect("no errors while waiting for exit");

        if !output.stdout.is_empty() {
            eprintln!("TEST: system updater stdout:\n{}", String::from_utf8_lossy(&output.stdout));
        }

        if !output.stderr.is_empty() {
            eprintln!("TEST: system updater stderr:\n{}", String::from_utf8_lossy(&output.stderr));
        }

        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        output.ok()
    }
}

struct SystemUpdaterArgs<'a> {
    initiator: &'a str,
    target: &'a str,
    update: Option<&'a str>,
    reboot: Option<bool>,
    skip_recovery: Option<bool>,
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
        while let Some(event) = stream.try_next().await? {
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

struct MockRebootService {
    interactions: SystemUpdaterInteractions,
}
impl MockRebootService {
    fn new(interactions: SystemUpdaterInteractions) -> Self {
        Self { interactions }
    }

    async fn run_reboot_service(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_hardware_power_statecontrol::AdminRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                fidl_fuchsia_hardware_power_statecontrol::AdminRequest::Reboot {
                    reason,
                    responder,
                } => {
                    assert_eq!(
                        reason,
                        fidl_fuchsia_hardware_power_statecontrol::RebootReason::SystemUpdate
                    );
                    eprintln!("TEST: Got reboot request with reason {:?}", reason);
                    self.interactions.lock().push(Reboot);
                    responder.send(&mut Ok(()))?;
                }
                _ => {
                    panic!("unhandled RebootService method {:?}", event);
                }
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
        while let Some(event) = stream.try_next().await? {
            let fidl_fuchsia_space::ManagerRequest::Gc { responder } = event;
            self.interactions.lock().push(Gc);
            responder.send(&mut Ok(()))?;
        }

        Ok(())
    }
}

#[derive(Clone)]
struct CustomEvent {
    metric_id: u32,
    values: Vec<fidl_fuchsia_cobalt::CustomEventValue>,
}

struct MockLogger {
    cobalt_events: Mutex<Vec<fidl_fuchsia_cobalt::CobaltEvent>>,
}

impl MockLogger {
    fn new() -> Self {
        Self { cobalt_events: Mutex::new(vec![]) }
    }

    async fn run_logger(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_cobalt::LoggerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                fidl_fuchsia_cobalt::LoggerRequest::LogCobaltEvent { event, responder } => {
                    self.cobalt_events.lock().push(event);
                    responder.send(fidl_fuchsia_cobalt::Status::Ok)?;
                }
                _ => {
                    panic!("unhandled Logger method {:?}", event);
                }
            }
        }

        Ok(())
    }
}

struct MockLoggerFactory {
    loggers: Mutex<Vec<Arc<MockLogger>>>,
    broken: Mutex<bool>,
}

impl MockLoggerFactory {
    fn new() -> Self {
        Self { loggers: Mutex::new(vec![]), broken: Mutex::new(false) }
    }

    async fn run_logger_factory(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_cobalt::LoggerFactoryRequestStream,
    ) -> Result<(), Error> {
        if *self.broken.lock() {
            eprintln!("TEST: This LoggerFactory is broken by order of the test.");
            // Drop the stream, closing the channel.
            return Ok(());
        }
        while let Some(event) = stream.try_next().await? {
            match event {
                fidl_fuchsia_cobalt::LoggerFactoryRequest::CreateLoggerFromProjectId {
                    project_id,
                    logger,
                    responder,
                } => {
                    eprintln!("TEST: Got CreateLogger request with project_id {:?}", project_id);
                    let mock_logger = Arc::new(MockLogger::new());
                    self.loggers.lock().push(Arc::clone(&mock_logger));
                    fasync::spawn(
                        mock_logger
                            .run_logger(logger.into_stream()?)
                            .unwrap_or_else(|e| eprintln!("error while running Logger: {:?}", e)),
                    );
                    responder.send(fidl_fuchsia_cobalt::Status::Ok)?;
                }
                _ => {
                    panic!("unhandled LoggerFactory method: {:?}", event);
                }
            }
        }

        Ok(())
    }
}

#[derive(PartialEq, Eq, Debug)]
struct OtaMetrics {
    initiator: u32,
    phase: u32,
    status_code: u32,
    target: String,
    // TODO: support free_space_delta assertions
}

impl OtaMetrics {
    fn from_events(mut events: Vec<fidl_fuchsia_cobalt::CobaltEvent>) -> Self {
        events.sort_by_key(|e| e.metric_id);

        // expecting one of each event
        assert_eq!(
            events.iter().map(|e| e.metric_id).collect::<Vec<_>>(),
            vec![
                metrics::OTA_START_METRIC_ID,
                metrics::OTA_RESULT_ATTEMPTS_METRIC_ID,
                metrics::OTA_RESULT_DURATION_METRIC_ID,
                metrics::OTA_RESULT_FREE_SPACE_DELTA_METRIC_ID
            ]
        );

        // we just asserted that we have the exact 4 things we're expecting, so unwrap them
        let mut iter = events.into_iter();
        let start = iter.next().unwrap();
        let attempt = iter.next().unwrap();
        let duration = iter.next().unwrap();
        let free_space_delta = iter.next().unwrap();

        // Some basic sanity checks follow
        assert_eq!(
            attempt.payload,
            fidl_fuchsia_cobalt::EventPayload::EventCount(fidl_fuchsia_cobalt::CountEvent {
                period_duration_micros: 0,
                count: 1
            })
        );

        let fidl_fuchsia_cobalt::CobaltEvent { event_codes, component, .. } = attempt;

        // metric event_codes and component should line up across all 3 result metrics
        assert_eq!(&duration.event_codes, &event_codes);
        assert_eq!(&duration.component, &component);
        assert_eq!(&free_space_delta.event_codes, &event_codes);
        assert_eq!(&free_space_delta.component, &component);

        // OtaStart only has initiator and hour_of_day, so just check initiator.
        assert_eq!(start.event_codes[0], event_codes[0]);
        assert_eq!(&start.component, &component);

        let target = component.expect("a target update merkle");

        assert_eq!(event_codes.len(), 3);
        let initiator = event_codes[0];
        let phase = event_codes[1];
        let status_code = event_codes[2];

        match duration.payload {
            fidl_fuchsia_cobalt::EventPayload::ElapsedMicros(_time) => {
                // Ignore the value since timing is not predictable.
            }
            other => {
                panic!("unexpected duration payload {:?}", other);
            }
        }

        // Ignore this for now, since it's using a shared tempdir, the values
        // are not deterministic.
        let _free_space_delta = match free_space_delta.payload {
            fidl_fuchsia_cobalt::EventPayload::EventCount(fidl_fuchsia_cobalt::CountEvent {
                period_duration_micros: 0,
                count,
            }) => count,
            other => {
                panic!("unexpected free space delta payload {:?}", other);
            }
        };

        Self { initiator, phase, status_code, target }
    }
}

const SYSTEM_IMAGE_URL: &str = "fuchsia-pkg://fuchsia.com/system_image/0?hash=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296";
const UPDATE_PKG_URL: &str = "fuchsia-pkg://fuchsia.com/update";

fn resolved_urls(interactions: SystemUpdaterInteractions) -> Vec<String> {
    (*interactions.lock())
        .iter()
        .filter_map(|interaction| match interaction {
            PackageResolve(package_url) => Some(package_url.clone()),
            _ => None,
        })
        .collect()
}
