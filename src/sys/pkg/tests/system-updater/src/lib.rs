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
    fidl_fuchsia_update_installer::{InstallerMarker, InstallerProxy},
    fidl_fuchsia_update_installer_ext::Options,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
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
        collections::HashSet,
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
mod history;
mod mode_force_recovery;
mod mode_normal;
mod options;
mod progress_reporting;
mod reboot_controller;
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

#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy)]
enum Protocol {
    PackageResolver,
    PackageCache,
    SpaceManager,
    Paver,
    Reboot,
    Cobalt,
}

type SystemUpdaterInteractions = Arc<Mutex<Vec<SystemUpdaterInteraction>>>;

struct TestEnvBuilder {
    paver_service_builder: MockPaverServiceBuilder,
    blocked_protocols: HashSet<Protocol>,
    oneshot: bool,
    history: Option<serde_json::Value>,
}

impl TestEnvBuilder {
    fn new() -> Self {
        TestEnvBuilder {
            paver_service_builder: MockPaverServiceBuilder::new(),
            blocked_protocols: HashSet::new(),
            oneshot: false,
            history: None,
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

    fn oneshot(mut self, oneshot: bool) -> Self {
        self.oneshot = oneshot;
        self
    }

    fn history(mut self, history: serde_json::Value) -> Self {
        self.history = Some(history);
        self
    }

    fn build(self) -> TestEnv {
        let Self { paver_service_builder, blocked_protocols, oneshot, history } = self;

        // A buffer to store all the interactions the system-updater has with external services.
        let interactions = Arc::new(Mutex::new(vec![]));

        let test_dir = TempDir::new().expect("create test tempdir");

        let data_path = test_dir.path().join("data");
        create_dir(&data_path).expect("create data dir");

        let build_info_path = test_dir.path().join("build-info");
        create_dir(&build_info_path).expect("create build-info dir");

        let misc_path = test_dir.path().join("misc");
        create_dir(&misc_path).expect("create misc dir");

        // Optionally write the pre-configured update history.
        if let Some(history) = history {
            serde_json::to_writer(
                File::create(data_path.join("update_history.json")).unwrap(),
                &history,
            )
            .unwrap()
        }

        // Set up system-updater to run in --oneshot false code path.
        let mut system_updater_builder = None;
        if !oneshot {
            system_updater_builder = Some(system_updater_app_builder(
                &data_path,
                &build_info_path,
                &misc_path,
                RawSystemUpdaterArgs(&[]),
                Default::default(),
            ));
        }

        // Set up the paver service to push events to our interactions buffer by overriding
        // call_hook and firmware_hook.
        let interactions_paver_clone = Arc::clone(&interactions);
        let paver_service = Arc::new(
            paver_service_builder
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

            let should_register = |protocol: Protocol| !blocked_protocols.contains(&protocol);

            if should_register(Protocol::PackageResolver) {
                fs.add_fidl_service(move |stream: PackageResolverRequestStream| {
                    fasync::Task::spawn(
                        Arc::clone(&resolver)
                            .run_resolver_service(stream)
                            .unwrap_or_else(|e| panic!("error running resolver service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::Paver) {
                fs.add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&paver_service)
                            .run_paver_service(stream)
                            .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::Reboot) {
                fs.add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&reboot_service)
                            .run_reboot_service(stream)
                            .unwrap_or_else(|e| panic!("error running reboot service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::PackageCache) {
                fs.add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&cache_service)
                            .run_cache_service(stream)
                            .unwrap_or_else(|e| panic!("error running cache service: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::Cobalt) {
                fs.add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&logger_factory)
                            .run_logger_factory(stream)
                            .unwrap_or_else(|e| panic!("error running logger factory: {:?}", e)),
                    )
                    .detach()
                });
            }
            if should_register(Protocol::SpaceManager) {
                fs.add_fidl_service(move |stream| {
                    fasync::Task::spawn(
                        Arc::clone(&space_service)
                            .run_space_service(stream)
                            .unwrap_or_else(|e| panic!("error running space service: {:?}", e)),
                    )
                    .detach()
                });
            }
        }

        let env = fs
            .create_salted_nested_environment("systemupdater_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        let system_updater = system_updater_builder
            .map(|builder| builder.spawn(env.launcher()).expect("sustem updater to launch"));

        TestEnv {
            env,
            resolver,
            _paver_service: paver_service,
            _reboot_service: reboot_service,
            cache_service,
            logger_factory,
            _space_service: space_service,
            _test_dir: test_dir,
            data_path,
            build_info_path,
            misc_path,
            interactions,
            system_updater,
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
    data_path: PathBuf,
    build_info_path: PathBuf,
    misc_path: PathBuf,
    interactions: SystemUpdaterInteractions,
    system_updater: Option<App>,
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

    /// Set the version of the build that system-updater is running on.
    fn set_build_version(&self, version: impl AsRef<str>) {
        // Write the "version" file into the build-info directory.
        let mut file =
            File::create(self.build_info_path.join("version")).expect("create version file");
        file.write_all(version.as_ref().as_bytes()).expect("write version file");
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

    async fn run_system_updater_oneshot<'a>(
        &self,
        args: SystemUpdaterArgs<'a>,
    ) -> Result<(), fuchsia_component::client::OutputError> {
        self.run_system_updater_oneshot_args(args, Default::default()).await
    }

    async fn run_system_updater_oneshot_args<'a>(
        &'a self,
        args: impl ToSystemUpdaterCliArgs,
        env: SystemUpdaterEnv,
    ) -> Result<(), fuchsia_component::client::OutputError> {
        let launcher = self.launcher();

        let output = system_updater_app_builder(
            &self.data_path,
            &self.build_info_path,
            &self.misc_path,
            args,
            env,
        )
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

    /// Opens a connection to the installer fidl service, panicking if the system updater was not
    /// started as a fidl service.
    fn installer_proxy(&self) -> InstallerProxy {
        self.system_updater.as_ref().unwrap().connect_to_service::<InstallerMarker>().unwrap()
    }
}

fn system_updater_app_builder(
    data_path: &PathBuf,
    build_info_path: &PathBuf,
    misc_path: &PathBuf,
    args: impl ToSystemUpdaterCliArgs,
    env: SystemUpdaterEnv,
) -> AppBuilder {
    let data_dir = File::open(data_path).expect("open data dir");
    let build_info_dir = File::open(build_info_path).expect("open config dir");
    let misc_dir = File::open(misc_path).expect("open misc dir");

    let mut system_updater = AppBuilder::new(
        "fuchsia-pkg://fuchsia.com/system-updater-integration-tests#meta/system-updater.cmx",
    )
    .add_dir_to_namespace("/config/build-info".to_string(), build_info_dir)
    .expect("/config/build-info to mount")
    .add_dir_to_namespace("/misc".to_string(), misc_dir)
    .expect("/misc to mount")
    .args(args.to_args());

    if env.mount_data {
        system_updater = system_updater
            .add_dir_to_namespace("/data".to_string(), data_dir)
            .expect("/data to mount");
    }

    system_updater
}

trait ToSystemUpdaterCliArgs {
    fn to_args(self) -> Vec<String>;
}

impl ToSystemUpdaterCliArgs for SystemUpdaterArgs<'_> {
    fn to_args(self) -> Vec<String> {
        self.build()
    }
}

impl ToSystemUpdaterCliArgs for RawSystemUpdaterArgs {
    fn to_args(self) -> Vec<String> {
        self.0.into_iter().map(|s| (*s).to_owned()).collect()
    }
}

#[derive(Debug)]
struct SystemUpdaterEnv {
    mount_data: bool,
}

impl Default for SystemUpdaterEnv {
    fn default() -> Self {
        Self { mount_data: true }
    }
}

#[derive(Debug)]
struct RawSystemUpdaterArgs(&'static [&'static str]);

#[derive(Debug, Default)]
struct SystemUpdaterArgs<'a> {
    initiator: Option<Initiator>,
    source: Option<&'a str>,
    target: Option<&'a str>,
    start: Option<i64>,
    update: Option<&'a str>,
    reboot: Option<bool>,
    skip_recovery: Option<bool>,
}

#[derive(Debug, Clone, Copy)]
enum Initiator {
    User,
    Service,
}

impl Default for Initiator {
    fn default() -> Self {
        Initiator::Service
    }
}

impl Initiator {
    fn to_cli_arg(self) -> String {
        match self {
            Initiator::User => "manual".to_string(),
            Initiator::Service => "automatic".to_string(),
        }
    }
}

impl SystemUpdaterArgs<'_> {
    fn build(&self) -> Vec<String> {
        let mut args = vec![];

        if let Some(initiator) = self.initiator {
            args.extend(vec!["--initiator".to_owned(), initiator.to_cli_arg()]);
        }
        if let Some(source) = self.source {
            args.extend(vec!["--source".to_owned(), source.to_owned()]);
        }
        if let Some(target) = self.target {
            args.extend(vec!["--target".to_owned(), target.to_owned()]);
        }
        if let Some(start) = self.start {
            args.extend(vec!["--start".to_owned(), start.to_string()]);
        }
        if let Some(update) = self.update {
            args.extend(vec!["--update".to_owned(), update.to_owned()]);
        }
        if let Some(reboot) = self.reboot {
            args.extend(vec!["--reboot".to_owned(), reboot.to_string()]);
        }
        if let Some(skip_recovery) = self.skip_recovery {
            args.extend(vec!["--skip-recovery".to_owned(), skip_recovery.to_string()]);
        }
        args.extend(vec!["--oneshot".to_owned(), "true".to_owned()]);

        args
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
}

impl MockLoggerFactory {
    fn new() -> Self {
        Self { loggers: Mutex::new(vec![]) }
    }

    async fn run_logger_factory(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_cobalt::LoggerFactoryRequestStream,
    ) -> Result<(), Error> {
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
                    fasync::Task::spawn(
                        mock_logger
                            .run_logger(logger.into_stream()?)
                            .unwrap_or_else(|e| eprintln!("error while running Logger: {:?}", e)),
                    )
                    .detach();
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
            ]
        );

        // we just asserted that we have the exact 4 things we're expecting, so unwrap them
        let mut iter = events.into_iter();
        let start = iter.next().unwrap();
        let attempt = iter.next().unwrap();
        let duration = iter.next().unwrap();

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

        Self { initiator, phase, status_code, target }
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

fn make_packages_json<'a>(urls: impl AsRef<[&'a str]>) -> String {
    json!({
      // TODO(50754): Change to "1" once we remove support for versions as ints.
      "version": 1,
      "content": urls.as_ref(),
    })
    .to_string()
}

const UPDATE_HASH: &str = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";
const SYSTEM_IMAGE_HASH: &str = "42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296";
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

fn default_options() -> Options {
    Options {
        initiator: fidl_fuchsia_update_installer_ext::Initiator::User,
        allow_attach_to_existing_attempt: true,
        should_write_recovery: true,
    }
}
