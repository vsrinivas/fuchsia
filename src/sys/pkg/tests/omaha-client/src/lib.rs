// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![cfg(test)]
use {
    anyhow::anyhow,
    assert_matches::assert_matches,
    diagnostics_reader::{ArchiveReader, Inspect},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_paver::{self as paver, PaverRequestStream},
    fidl_fuchsia_pkg::{PackageCacheRequestStream, PackageResolverRequestStream},
    fidl_fuchsia_update::{
        AttemptsMonitorMarker, AttemptsMonitorRequest, AttemptsMonitorRequestStream,
        CheckNotStartedReason, CheckOptions, CheckingForUpdatesData, CommitStatusProviderMarker,
        CommitStatusProviderProxy, ErrorCheckingForUpdateData, Initiator,
        InstallationDeferralReason, InstallationDeferredData, InstallationErrorData,
        InstallationProgress, InstallingData, ManagerMarker, ManagerProxy, MonitorMarker,
        MonitorRequest, MonitorRequestStream, NoUpdateAvailableData, State, UpdateInfo,
    },
    fidl_fuchsia_update_channelcontrol::{ChannelControlMarker, ChannelControlProxy},
    fidl_fuchsia_update_installer::UpdateNotStartedReason,
    fidl_fuchsia_update_installer_ext as installer, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_inspect::{
        assert_data_tree,
        reader::DiagnosticsHierarchy,
        testing::{AnyProperty, TreeAssertion},
        tree_assertion,
    },
    fuchsia_pkg_testing::{make_current_epoch_json, make_packages_json},
    fuchsia_url::{PinnedAbsolutePackageUrl, UnpinnedAbsolutePackageUrl},
    fuchsia_zircon as zx,
    futures::{
        channel::{mpsc, oneshot},
        prelude::*,
    },
    mock_crash_reporter::{CrashReport, MockCrashReporterService, ThrottleHook},
    mock_installer::MockUpdateInstallerService,
    mock_omaha_server::{
        OmahaResponse, OmahaServer, OmahaServerBuilder, PrivateKeyAndId, PrivateKeys,
        ResponseAndMetadata,
    },
    mock_paver::{hooks as mphooks, MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_reboot::{MockRebootService, RebootReason},
    mock_resolver::MockResolverService,
    mock_verifier::MockVerifierService,
    omaha_client::cup_ecdsa::test_support::{
        make_default_private_key_for_test, RAW_PUBLIC_KEY_FOR_TEST,
    },
    parking_lot::Mutex,
    serde_json::json,
    std::{
        collections::HashMap,
        convert::TryInto,
        fs::{self, create_dir},
        path::PathBuf,
        sync::Arc,
    },
    tempfile::TempDir,
};

const OMAHA_CLIENT_CML: &str = "#meta/omaha-client-service.cm";
const SYSTEM_UPDATER_CML: &str = "#meta/system-updater.cm";
const SYSTEM_UPDATE_COMMITTER_CML: &str = "#meta/system-update-committer.cm";
const STASH_CML: &str = "#meta/stash2.cm";

struct Mounts {
    _test_dir: TempDir,
    config_data: PathBuf,
    build_info: PathBuf,
}

impl Mounts {
    fn new() -> Self {
        let test_dir = TempDir::new().expect("create test tempdir");
        let config_data = test_dir.path().join("config_data");
        create_dir(&config_data).expect("create config_data dir");
        let build_info = test_dir.path().join("build_info");
        create_dir(&build_info).expect("create build_info dir");

        Self { _test_dir: test_dir, config_data, build_info }
    }

    fn write_url(&self, url: impl AsRef<[u8]>) {
        let url_path = self.config_data.join("omaha_url");
        fs::write(url_path, url).expect("write omaha_url");
    }

    fn write_appid(&self, appid: impl AsRef<[u8]>) {
        let appid_path = self.config_data.join("omaha_app_id");
        fs::write(appid_path, appid).expect("write omaha_app_id");
    }

    fn write_version(&self, version: impl AsRef<[u8]>) {
        let version_path = self.build_info.join("version");
        fs::write(version_path, version).expect("write version");
    }

    fn write_eager_package_config(&self, config: impl AsRef<[u8]>) {
        let eager_package_config_path = self.config_data.join("eager_package_config.json");
        fs::write(eager_package_config_path, config).expect("write eager_package_config.json");
    }
}
struct Proxies {
    _cache: Arc<MockCache>,
    config_optout: Arc<fuchsia_update_config_optout::Mock>,
    resolver: Arc<MockResolverService>,
    update_manager: ManagerProxy,
    channel_control: ChannelControlProxy,
    commit_status_provider: CommitStatusProviderProxy,
    _verifier: Arc<MockVerifierService>,
}

// A builder lambda which accepts as input the full service URL of the mock
// Omaha server and returns eager package config as JSON.
type EagerPackageConfigBuilder = fn(&str) -> serde_json::Value;

struct TestEnvBuilder {
    // Set one of responses, responses_and_metadata.
    responses_by_appid: Vec<(String, ResponseAndMetadata)>,
    version: String,
    installer: Option<MockUpdateInstallerService>,
    paver: Option<MockPaverService>,
    crash_reporter: Option<MockCrashReporterService>,
    eager_package_config_builder: Option<EagerPackageConfigBuilder>,
    omaha_client_config_bool_overrides: Vec<(String, bool)>,
    omaha_client_config_uint16_overrides: Vec<(String, u16)>,
    cup_info_map: HashMap<UnpinnedAbsolutePackageUrl, (String, String)>,
    private_keys: Option<PrivateKeys>,
    etag_override: Option<String>,
}

impl TestEnvBuilder {
    fn new() -> Self {
        Self {
            responses_by_appid: vec![(
                "integration-test-appid".to_string(),
                ResponseAndMetadata::default(),
            )],
            version: "0.1.2.3".to_string(),
            installer: None,
            paver: None,
            crash_reporter: None,
            eager_package_config_builder: None,
            omaha_client_config_bool_overrides: vec![],
            omaha_client_config_uint16_overrides: vec![],
            cup_info_map: HashMap::new(),
            private_keys: None,
            etag_override: None,
        }
    }

    fn default_with_response(self, response: OmahaResponse) -> Self {
        Self {
            responses_by_appid: vec![(
                "integration-test-appid".to_string(),
                ResponseAndMetadata { response, ..Default::default() },
            )],
            ..self
        }
    }

    fn responses_and_metadata(
        self,
        responses_by_appid: Vec<(String, ResponseAndMetadata)>,
    ) -> Self {
        Self { responses_by_appid, ..self }
    }

    fn version(self, version: impl Into<String>) -> Self {
        Self { version: version.into(), ..self }
    }

    fn installer(self, installer: MockUpdateInstallerService) -> Self {
        Self { installer: Some(installer), ..self }
    }

    fn paver(self, paver: MockPaverService) -> Self {
        Self { paver: Some(paver), ..self }
    }

    fn crash_reporter(self, crash_reporter: MockCrashReporterService) -> Self {
        Self { crash_reporter: Some(crash_reporter), ..self }
    }

    fn eager_package_config_builder(
        self,
        eager_package_config_builder: EagerPackageConfigBuilder,
    ) -> Self {
        Self { eager_package_config_builder: Some(eager_package_config_builder), ..self }
    }

    fn omaha_client_override_config_bool(mut self, key: String, value: bool) -> Self {
        self.omaha_client_config_bool_overrides.push((key.into(), value));
        self
    }

    fn omaha_client_override_config_uint16(mut self, key: String, value: u16) -> Self {
        self.omaha_client_config_uint16_overrides.push((key.into(), value));
        self
    }

    fn add_cup_info(
        mut self,
        url: impl Into<String>,
        version: impl Into<String>,
        channel: impl Into<String>,
    ) -> Self {
        self.cup_info_map.insert(url.into().parse().unwrap(), (version.into(), channel.into()));
        self
    }

    fn private_keys(mut self, private_keys: PrivateKeys) -> Self {
        self.private_keys = Some(private_keys);
        self
    }

    fn etag_override(mut self, etag_override: impl Into<String>) -> Self {
        self.etag_override = Some(etag_override.into());
        self
    }

    async fn build(self) -> TestEnv {
        // Add the mount directories to fs service.
        let mounts = Mounts::new();
        let mut fs = ServiceFs::new();
        let config_data_path = mounts.config_data.clone().into_os_string().into_string().unwrap();
        let build_info_path = mounts.build_info.clone().into_os_string().into_string().unwrap();
        let config_data = fuchsia_fs::directory::open_in_namespace(
            config_data_path.as_str(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let build_info = fuchsia_fs::directory::open_in_namespace(
            build_info_path.as_str(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        fs.dir("config").add_remote("data", config_data);
        fs.dir("config").add_remote("build-info", build_info);

        let server = Arc::new(Mutex::new({
            let mut b = OmahaServerBuilder::default().responses_by_appid(
                self.responses_by_appid
                    .into_iter()
                    .collect::<HashMap<String, ResponseAndMetadata>>(),
            );
            if let Some(pk) = self.private_keys {
                b = b.private_keys(pk);
            }
            b.etag_override(self.etag_override).build().unwrap()
        }));
        let url = OmahaServer::start(server.clone()).expect("start server");
        mounts.write_url(&url);
        mounts.write_appid("integration-test-appid");
        mounts.write_version(self.version);
        if let Some(eager_package_config_builder) = self.eager_package_config_builder {
            let json = eager_package_config_builder(&url);
            mounts.write_eager_package_config(json.to_string());
        }

        let mut svc = fs.dir("svc");

        let paver = Arc::new(self.paver.unwrap_or_else(|| MockPaverServiceBuilder::new().build()));
        svc.add_fidl_service(move |stream: PaverRequestStream| {
            fasync::Task::spawn(
                Arc::clone(&paver)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach();
        });

        let resolver = Arc::new(MockResolverService::new(None));
        {
            let resolver = resolver.clone();
            svc.add_fidl_service(move |stream: PackageResolverRequestStream| {
                fasync::Task::spawn(
                    Arc::clone(&resolver).run_resolver_service(stream).unwrap_or_else(|e| {
                        panic!("error running resolver service {:#}", anyhow!(e))
                    }),
                )
                .detach()
            });
        }

        let cache = Arc::new(MockCache::new());
        {
            let cache = cache.clone();
            svc.add_fidl_service(move |stream: PackageCacheRequestStream| {
                fasync::Task::spawn(Arc::clone(&cache).run_cache_service(stream)).detach()
            });
        }

        let config_optout = Arc::new(fuchsia_update_config_optout::Mock::new());
        svc.add_fidl_service({
            let config_optout = Arc::clone(&config_optout);
            move |stream| fasync::Task::spawn(Arc::clone(&config_optout).serve(stream)).detach()
        });

        let cup = Arc::new(fuchsia_pkg_cup::Mock::new(self.cup_info_map));
        svc.add_fidl_service({
            let cup = Arc::clone(&cup);
            move |stream| fasync::Task::spawn(Arc::clone(&cup).serve(stream)).detach()
        });

        let (send, reboot_called) = oneshot::channel();
        let send = Mutex::new(Some(send));
        let reboot_service = Arc::new(MockRebootService::new(Box::new(move |reason| {
            assert_eq!(reason, RebootReason::SystemUpdate);
            send.lock().take().unwrap().send(()).unwrap();
            Ok(())
        })));
        svc.add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&reboot_service)
                    .run_reboot_service(stream)
                    .unwrap_or_else(|e| panic!("error running reboot service: {:#}", anyhow!(e))),
            )
            .detach()
        });

        // Set up verifier service.
        let verifier = Arc::new(MockVerifierService::new(|_| Ok(())));
        {
            let verifier = Arc::clone(&verifier);
            svc.add_fidl_service(move |stream| {
                fasync::Task::spawn(Arc::clone(&verifier).run_blobfs_verifier_service(stream))
                    .detach()
            });
        }

        // Set up crash reporter service.
        let crash_reporter = Arc::new(
            self.crash_reporter.unwrap_or_else(|| MockCrashReporterService::new(|_| Ok(()))),
        );
        {
            let crash_reporter = Arc::clone(&crash_reporter);
            svc.add_fidl_service(move |stream| {
                fasync::Task::spawn(Arc::clone(&crash_reporter).run_crash_reporter_service(stream))
                    .detach()
            });
        }

        let mut use_real_system_updater = true;
        if let Some(installer) = self.installer {
            use_real_system_updater = false;
            let installer = Arc::new(installer);
            {
                let installer = Arc::clone(&installer);
                svc.add_fidl_service(move |stream| {
                    fasync::Task::spawn(Arc::clone(&installer).run_service(stream)).detach()
                });
            }
        }

        let fs_holder = Mutex::new(Some(fs));
        let builder = RealmBuilder::new().await.expect("Failed to create test realm builder");
        let omaha_client_service = builder
            .add_child("omaha_client_service", OMAHA_CLIENT_CML, ChildOptions::new().eager())
            .await
            .unwrap();
        builder.init_mutable_config_from_package(&omaha_client_service).await.unwrap();
        for (k, v) in self.omaha_client_config_bool_overrides {
            builder.set_config_value_bool(&omaha_client_service, &k, v).await.unwrap();
        }
        for (k, v) in self.omaha_client_config_uint16_overrides {
            builder.set_config_value_uint16(&omaha_client_service, &k, v).await.unwrap();
        }

        let system_update_committer = builder
            .add_child(
                "system_update_committer",
                SYSTEM_UPDATE_COMMITTER_CML,
                ChildOptions::new().eager(),
            )
            .await
            .unwrap();
        let fake_capabilities = builder
            .add_local_child(
                "fake_capabilities",
                move |handles| {
                    let mut rfs = fs_holder
                        .lock()
                        .take()
                        .expect("mock component should only be launched once");
                    async {
                        let _ = &handles;
                        rfs.serve_connection(handles.outgoing_dir).unwrap();
                        let () = rfs.collect().await;
                        Ok(())
                    }
                    .boxed()
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();
        let stash2 = builder.add_child("stash2", STASH_CML, ChildOptions::new()).await.unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::storage("data"))
                    .from(Ref::parent())
                    .to(&stash2),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("config-data")
                            .path("/config/data")
                            .rights(fio::R_STAR_DIR),
                    )
                    .capability(Capability::protocol_by_name("fuchsia.paver.Paver"))
                    .capability(Capability::protocol_by_name(
                        "fuchsia.hardware.power.statecontrol.Admin",
                    ))
                    .from(&fake_capabilities)
                    .to(&omaha_client_service)
                    .to(&system_update_committer),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("build-info")
                            .path("/config/build-info")
                            .rights(fio::R_STAR_DIR),
                    )
                    .capability(Capability::protocol_by_name("fuchsia.ui.interaction.Notifier"))
                    .capability(Capability::protocol_by_name("fuchsia.feedback.CrashReporter"))
                    .capability(Capability::protocol_by_name(
                        "fuchsia.feedback.ComponentDataRegister",
                    ))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.Cup"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageResolver"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.rewrite.Engine"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.RepositoryManager"))
                    .capability(Capability::protocol_by_name("fuchsia.update.config.OptOut"))
                    .from(&fake_capabilities)
                    .to(&omaha_client_service),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("root-ssl-certificates"))
                    .capability(Capability::protocol_by_name(
                        "fuchsia.metrics.MetricEventLoggerFactory",
                    ))
                    .from(Ref::parent())
                    .to(&omaha_client_service),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&omaha_client_service)
                    .to(&system_update_committer)
                    .to(&stash2),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.update.verify.BlobfsVerifier",
                    ))
                    .from(&fake_capabilities)
                    .to(&system_update_committer),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.posix.socket.Provider"))
                    .capability(Capability::protocol_by_name("fuchsia.net.name.Lookup"))
                    .from(Ref::parent())
                    .to(&omaha_client_service)
                    .to(&system_update_committer),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.stash.Store2"))
                    .from(&stash2)
                    .to(&omaha_client_service),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.update.CommitStatusProvider"))
                    .from(&system_update_committer)
                    .to(&omaha_client_service)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.update.channel.Provider"))
                    .capability(Capability::protocol_by_name(
                        "fuchsia.update.channelcontrol.ChannelControl",
                    ))
                    .capability(Capability::protocol_by_name("fuchsia.update.Manager"))
                    .from(&omaha_client_service)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        if use_real_system_updater {
            let system_updater = builder
                .add_child("system_updater", SYSTEM_UPDATER_CML, ChildOptions::new().eager())
                .await
                .unwrap();
            builder
                .add_route(
                    Route::new()
                        .capability(
                            Capability::directory("config-data")
                                .path("/config/data")
                                .rights(fio::R_STAR_DIR),
                        )
                        .capability(Capability::protocol_by_name("fuchsia.paver.Paver"))
                        .capability(Capability::protocol_by_name("fuchsia.pkg.PackageCache"))
                        .capability(Capability::protocol_by_name("fuchsia.pkg.PackageResolver"))
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
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::storage("data"))
                        .capability(Capability::protocol_by_name(
                            "fuchsia.metrics.MetricEventLoggerFactory",
                        ))
                        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                        .from(Ref::parent())
                        .to(&system_updater),
                )
                .await
                .unwrap();
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(
                            "fuchsia.update.installer.Installer",
                        ))
                        .from(&system_updater)
                        .to(&omaha_client_service),
                )
                .await
                .unwrap();
        } else {
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(
                            "fuchsia.update.installer.Installer",
                        ))
                        .from(&fake_capabilities)
                        .to(&omaha_client_service),
                )
                .await
                .unwrap();
        }

        let realm_instance = builder.build().await.unwrap();
        let channel_control = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<ChannelControlMarker>()
            .expect("connect to channel control provider");
        let update_manager = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<ManagerMarker>()
            .expect("connect to update manager");
        let commit_status_provider = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<CommitStatusProviderMarker>()
            .expect("connect to commit status provider");

        TestEnv {
            realm_instance,
            _mounts: mounts,
            proxies: Proxies {
                _cache: cache,
                config_optout,
                resolver,
                update_manager,
                channel_control,
                commit_status_provider,
                _verifier: verifier,
            },
            server,
            reboot_called,
        }
    }
}

struct TestEnv {
    realm_instance: RealmInstance,
    _mounts: Mounts,
    proxies: Proxies,
    server: Arc<Mutex<OmahaServer>>,
    reboot_called: oneshot::Receiver<()>,
}

impl TestEnv {
    async fn check_now(&self) -> MonitorRequestStream {
        let options = CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(false),
            ..CheckOptions::EMPTY
        };
        let (client_end, stream) =
            fidl::endpoints::create_request_stream::<MonitorMarker>().unwrap();
        self.proxies
            .update_manager
            .check_now(options, Some(client_end))
            .await
            .expect("make check_now call")
            .expect("check started");
        stream
    }

    async fn monitor_all_update_checks(&self) -> AttemptsMonitorRequestStream {
        let (client_end, stream) =
            fidl::endpoints::create_request_stream::<AttemptsMonitorMarker>().unwrap();
        self.proxies
            .update_manager
            .monitor_all_update_checks(client_end)
            .expect("make monitor_all_update call");
        stream
    }

    async fn perform_pending_reboot(&self) -> bool {
        self.proxies
            .update_manager
            .perform_pending_reboot()
            .await
            .expect("make perform_pending_reboot call")
    }

    async fn inspect_hierarchy(&self) -> DiagnosticsHierarchy {
        let nested_environment_label = format!(
            "test_driver/realm_builder\\:{}/omaha_client_service:root",
            self.realm_instance.root.child_name()
        );
        ArchiveReader::new()
            .add_selector(nested_environment_label.to_string())
            .snapshot::<Inspect>()
            .await
            .expect("read inspect hierarchy")
            .into_iter()
            .next()
            .expect("one result")
            .payload
            .expect("payload is not none")
    }

    async fn assert_platform_metrics(&self, children: TreeAssertion) {
        assert_data_tree!(
            self.inspect_hierarchy().await,
            "root": contains {
                "platform_metrics": contains {
                    "events": contains {
                        "capacity": 50u64,
                        children,
                    }
                }
            }
        );
    }
}

struct MockCache;

impl MockCache {
    fn new() -> Self {
        Self
    }
    async fn run_cache_service(self: Arc<Self>, mut stream: PackageCacheRequestStream) {
        while let Some(request) = stream.try_next().await.unwrap() {
            match request {
                fidl_fuchsia_pkg::PackageCacheRequest::Sync { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("unsupported PackageCache request: {:?}", other),
            }
        }
    }
}

pub mod fuchsia_update_config_optout {
    use super::*;
    pub use fidl_fuchsia_update_config::OptOutPreference;
    use fidl_fuchsia_update_config::{OptOutRequest, OptOutRequestStream};

    #[derive(Debug)]
    pub struct Mock(Mutex<OptOutPreference>);

    impl Mock {
        pub fn new() -> Self {
            Self(Mutex::new(OptOutPreference::AllowAllUpdates))
        }

        pub fn set(&self, value: OptOutPreference) {
            *self.0.lock() = value;
        }

        pub async fn serve(self: Arc<Self>, mut stream: OptOutRequestStream) {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    OptOutRequest::Get { responder } => {
                        let value = *self.0.lock();
                        if let Err(e) = responder.send(value) {
                            eprintln!("fuchsia_update_config_optout::Mock::serve() failed to send a response, possibly because the client is shut down: {:?}", e);
                        }
                    }
                }
            }
        }
    }
}

pub mod fuchsia_pkg_cup {
    use super::*;
    use fidl_fuchsia_pkg::{CupRequest, CupRequestStream};

    #[derive(Debug)]
    pub struct Mock {
        info_map: HashMap<UnpinnedAbsolutePackageUrl, (String, String)>,
    }

    impl Mock {
        pub fn new(info_map: HashMap<UnpinnedAbsolutePackageUrl, (String, String)>) -> Self {
            Self { info_map }
        }

        pub async fn serve(self: Arc<Self>, mut stream: CupRequestStream) {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    CupRequest::Write { url, cup, responder } => {
                        let url: PinnedAbsolutePackageUrl = url.url.parse().unwrap();
                        assert_eq!(url.host(), "integration.test.fuchsia.com");
                        assert_eq!(
                            url.hash().to_string(),
                            "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
                        );
                        assert!(cup.request.is_some());
                        assert!(cup.key_id.is_some());
                        assert!(cup.nonce.is_some());
                        assert!(cup.response.is_some());
                        assert!(cup.signature.is_some());
                        responder.send(&mut Ok(())).unwrap();
                    }
                    CupRequest::GetInfo { url, responder } => {
                        let response = self.info_map[&url.url.parse().unwrap()].clone();
                        responder.send(&mut Ok(response)).unwrap();
                    }
                }
            }
        }
    }
}

async fn expect_states(stream: &mut MonitorRequestStream, expected_states: &[State]) {
    for expected_state in expected_states {
        let MonitorRequest::OnState { state, responder } =
            stream.try_next().await.unwrap().unwrap();
        assert_eq!(&state, expected_state);
        responder.send().unwrap();
    }
}

fn update_info() -> Option<UpdateInfo> {
    // TODO(fxbug.dev/47469): version_available should be `Some("0.1.2.3".to_string())` once omaha-client
    // returns version_available.
    Some(UpdateInfo { version_available: None, download_size: None, ..UpdateInfo::EMPTY })
}

fn progress(fraction_completed: Option<f32>) -> Option<InstallationProgress> {
    Some(InstallationProgress { fraction_completed, ..InstallationProgress::EMPTY })
}

async fn omaha_client_update(
    mut env: TestEnv,
    platform_metrics: TreeAssertion,
    should_wait_for_reboot: bool,
) {
    env.proxies
        .resolver
        .url("fuchsia-pkg://integration.test.fuchsia.com/update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
        .resolve(
        &env.proxies
            .resolver
            .package("update", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
            .add_file(
                "packages.json",
                make_packages_json(["fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead"]),
            )
            .add_file("zbi", "fake zbi")
            .add_file("epoch.json", make_current_epoch_json())
    );
    env.proxies
        .resolver.url("fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead")
        .resolve(
        &env.proxies
            .resolver
            .package("system_image", "beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeada")
    );

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;
    let mut last_progress: Option<InstallationProgress> = None;
    let mut waiting_for_reboot = false;
    while let Some(request) = stream.try_next().await.unwrap() {
        let MonitorRequest::OnState { state, responder } = request;
        match state {
            State::InstallingUpdate(InstallingData { update, installation_progress, .. }) => {
                assert_eq!(update, update_info());
                assert!(!waiting_for_reboot);
                if let Some(last_progress) = last_progress {
                    let last = last_progress.fraction_completed.unwrap();
                    let current =
                        installation_progress.as_ref().unwrap().fraction_completed.unwrap();
                    assert!(
                        last <= current,
                        "progress is not increasing, last: {}, current: {}",
                        last,
                        current,
                    );
                }
                last_progress = installation_progress;
            }
            State::WaitingForReboot(InstallingData { update, installation_progress, .. }) => {
                assert_eq!(update, update_info());
                assert_eq!(installation_progress, progress(Some(1.)));
                assert!(!waiting_for_reboot);
                waiting_for_reboot = true;
                assert_matches!(env.reboot_called.try_recv(), Ok(None));
            }
            state => {
                panic!("Unexpected state: {:?}", state);
            }
        }
        responder.send().unwrap();
    }
    assert_matches!(last_progress, Some(_));
    assert_eq!(waiting_for_reboot, should_wait_for_reboot);

    env.assert_platform_metrics(platform_metrics).await;

    if should_wait_for_reboot {
        // This will hang if reboot was not triggered.
        env.reboot_called.await.unwrap();
    } else {
        assert_matches!(env.reboot_called.try_recv(), Ok(None));
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update() {
    let env = TestEnvBuilder::new().default_with_response(OmahaResponse::Update).build().await;
    omaha_client_update(
        env,
        tree_assertion!(
            "children": {
                "0": contains {
                    "event": "CheckingForUpdates",
                },
                "1": contains {
                    "event": "InstallingUpdate",
                    "target-version": "0.1.2.3",
                },
                "2": contains {
                    "event": "WaitingForReboot",
                    "target-version": "0.1.2.3",
                }
            }
        ),
        true,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_multi_app() {
    use omaha_client::cup_ecdsa::test_support::make_default_public_key_id_for_test;
    let env = TestEnvBuilder::new()
        .responses_and_metadata(vec![
            (
                "integration-test-appid".to_string(),
                ResponseAndMetadata { response: OmahaResponse::Update, ..Default::default() },
            ),
            (
                "foo".to_string(),
                ResponseAndMetadata {
                    response: OmahaResponse::NoUpdate,
                    version: Some("0.0.4.1".to_string()),
                    ..Default::default()
                },
            ),
            (
                "bar".to_string(),
                ResponseAndMetadata {
                    response: OmahaResponse::NoUpdate,
                    version: Some("0.0.4.2".to_string()),
                    ..Default::default()
                },
            ),
        ])
        .eager_package_config_builder(|url: &str| {
            json!(
            {
                "eager_package_configs": [
                    {
                        "server": {
                            "service_url": url,
                            "public_keys": {
                                "latest": {
                                    "id": make_default_public_key_id_for_test(),
                                    "key": RAW_PUBLIC_KEY_FOR_TEST,
                                },
                                "historical": [],
                            }
                        },
                        "packages":
                        [
                            {
                                "url": "fuchsia-pkg://example.com/package",
                                "flavor": "debug",
                                "channel_config":
                                    {
                                        "channels":
                                            [
                                                {
                                                    "name": "stable",
                                                    "repo": "stable",
                                                    "appid": "foo"
                                                },
                                            ],
                                        "default_channel": "stable"
                                    }
                            },
                            {
                                "url": "fuchsia-pkg://example.com/package2",
                                "channel_config":
                                    {
                                        "channels":
                                            [
                                                {
                                                    "name": "stable",
                                                    "repo": "stable",
                                                    "appid": "bar"
                                                }
                                            ],
                                        "default_channel": "stable"
                                    }
                            }
                        ]
                    }
                ]
            })
        })
        .add_cup_info("fuchsia-pkg://example.com/package", "0.0.4.1", "stable")
        .add_cup_info("fuchsia-pkg://example.com/package2", "0.0.4.2", "stable")
        .build()
        .await;
    omaha_client_update(
        env,
        tree_assertion!(
            "children": {
                "0": contains {
                    "event": "CheckingForUpdates",
                },
                "1": contains {
                    "event": "InstallingUpdate",
                    "target-version": "0.1.2.3",
                },
                "2": contains {
                    "event": "WaitingForReboot",
                    "target-version": "0.1.2.3",
                }
            }
        ),
        true,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_eager_package() {
    let env = TestEnvBuilder::new()
        .responses_and_metadata(vec![
            (
                "integration-test-appid".to_string(),
                ResponseAndMetadata { response: OmahaResponse::NoUpdate, ..Default::default() },
            ),
            (
                "foo".to_string(),
                ResponseAndMetadata {
                    response: OmahaResponse::Update,
                    version: Some("0.0.4.1".to_string()),
                    ..Default::default()
                },
            ),
            (
                "bar".to_string(),
                ResponseAndMetadata {
                    response: OmahaResponse::NoUpdate,
                    version: Some("0.0.4.2".to_string()),
                    ..Default::default()
                },
            ),
        ])
        .private_keys(PrivateKeys {
            latest: PrivateKeyAndId {
                id: 100_i32.try_into().unwrap(),
                key: make_default_private_key_for_test(),
            },
            historical: vec![PrivateKeyAndId {
                id: 42_i32.try_into().unwrap(),
                key: make_default_private_key_for_test(),
            }],
        })
        .eager_package_config_builder(|url: &str| {
            json!(
            {
                "eager_package_configs": [
                    {
                        "server": {
                            "service_url": url,
                            "public_keys": {
                                "latest": {
                                    "id": 42,
                                    "key": RAW_PUBLIC_KEY_FOR_TEST,
                                },
                                "historical": [],
                            }
                        },
                        "packages":
                        [
                            {
                                "url": "fuchsia-pkg://example.com/package",
                                "flavor": "debug",
                                "channel_config":
                                    {
                                        "channels":
                                            [
                                                {
                                                    "name": "stable",
                                                    "repo": "stable",
                                                    "appid": "foo"
                                                },
                                            ],
                                        "default_channel": "stable"
                                    }
                            },
                            {
                                "url": "fuchsia-pkg://example.com/package2",
                                "channel_config":
                                    {
                                        "channels":
                                            [
                                                {
                                                    "name": "stable",
                                                    "repo": "stable",
                                                    "appid": "bar"
                                                }
                                            ]
                                    }
                            }
                        ]
                    }
                ]
            })
        })
        .add_cup_info("fuchsia-pkg://example.com/package", "0.0.4.1", "stable")
        .add_cup_info("fuchsia-pkg://example.com/package2", "0.0.4.2", "stable")
        .build()
        .await;
    omaha_client_update(
        env,
        tree_assertion!(
            "children": {
                "0": contains {
                    "event": "CheckingForUpdates",
                },
                "1": contains {
                    "event": "InstallingUpdate",
                    "target-version": "",
                },
            }
        ),
        false,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_cup_force_historical_key() {
    // This test forces usage of a historical key -- the server is passed a
    // private key struct has key #100 as the latest and #42 in the historical
    // vector, but the client is passed a public key struct which has key #42 as
    // the latest.
    let env = TestEnvBuilder::new()
        .responses_and_metadata(vec![(
            "integration-test-appid".to_string(),
            ResponseAndMetadata { response: OmahaResponse::Update, ..Default::default() },
        )])
        .private_keys(PrivateKeys {
            latest: PrivateKeyAndId {
                id: 100_i32.try_into().unwrap(),
                key: make_default_private_key_for_test(),
            },
            historical: vec![PrivateKeyAndId {
                id: 42_i32.try_into().unwrap(),
                key: make_default_private_key_for_test(),
            }],
        })
        .eager_package_config_builder(|url: &str| {
            json!(
            {
                "eager_package_configs": [
                    {
                        "server": {
                            "service_url": url,
                            "public_keys": {
                                "latest": {
                                    "id": 42,
                                    "key": RAW_PUBLIC_KEY_FOR_TEST,
                                },
                                "historical": [],
                            }
                        },
                        "packages": [ ]
                    }
                ]
            })
        })
        .build()
        .await;
    omaha_client_update(
        env,
        tree_assertion!(
            "children": {
                "0": contains {
                    "event": "CheckingForUpdates",
                },
                "1": contains {
                    "event": "InstallingUpdate",
                    "target-version": "0.1.2.3",
                },
                "2": contains {
                    "event": "WaitingForReboot",
                    "target-version": "0.1.2.3",
                }
            }
        ),
        true,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_cup_key_mismatch() {
    // If the server and client don't share a public/private keypair, no
    // handshake and no response.
    let env = TestEnvBuilder::new()
        .responses_and_metadata(vec![(
            "integration-test-appid".to_string(),
            ResponseAndMetadata { response: OmahaResponse::Update, ..Default::default() },
        )])
        .private_keys(PrivateKeys {
            latest: PrivateKeyAndId {
                id: 100_i32.try_into().unwrap(),
                key: make_default_private_key_for_test(),
            },
            historical: vec![],
        })
        .eager_package_config_builder(|url: &str| {
            json!(
            {
                "eager_package_configs": [
                    {
                        "server": {
                            "service_url": url,
                            "public_keys": {
                                "latest": {
                                    "id": 42,
                                    "key": RAW_PUBLIC_KEY_FOR_TEST,
                                },
                                "historical": [],
                            }
                        },
                        "packages": [ ]
                    }
                ]
            })
        })
        .build()
        .await;
    do_failed_update_check(&env).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_cup_bad_etag() {
    // What if the server returns an empty etag?
    let env = TestEnvBuilder::new()
        .responses_and_metadata(vec![(
            "integration-test-appid".to_string(),
            ResponseAndMetadata { response: OmahaResponse::Update, ..Default::default() },
        )])
        .etag_override("a1b2c3d4e5")
        .eager_package_config_builder(|url: &str| {
            json!(
            {
                "eager_package_configs": [
                    {
                        "server": {
                            "service_url": url,
                            "public_keys": {
                                "latest": {
                                    "id": 42,
                                    "key": RAW_PUBLIC_KEY_FOR_TEST,
                                },
                                "historical": [],
                            }
                        },
                        "packages": [ ]
                    }
                ]
            })
        })
        .build()
        .await;
    do_failed_update_check(&env).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_cup_empty_etag() {
    // What if the server returns an empty etag?
    let env = TestEnvBuilder::new()
        .responses_and_metadata(vec![(
            "integration-test-appid".to_string(),
            ResponseAndMetadata { response: OmahaResponse::Update, ..Default::default() },
        )])
        .etag_override("")
        .eager_package_config_builder(|url: &str| {
            json!(
            {
                "eager_package_configs": [
                    {
                        "server": {
                            "service_url": url,
                            "public_keys": {
                                "latest": {
                                    "id": 42,
                                    "key": RAW_PUBLIC_KEY_FOR_TEST,
                                },
                                "historical": [],
                            }
                        },
                        "packages": [ ]
                    }
                ]
            })
        })
        .build()
        .await;
    do_failed_update_check(&env).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_attempt_monitor_update_progress_with_mock_installer() {
    let (mut sender, receiver) = mpsc::channel(0);
    let installer = MockUpdateInstallerService::builder().states_receiver(receiver).build();
    let env = TestEnvBuilder::new()
        .default_with_response(OmahaResponse::Update)
        .installer(installer)
        .build()
        .await;

    env.check_now().await;
    let mut request_stream = env.monitor_all_update_checks().await;
    let AttemptsMonitorRequest::OnStart { options, monitor, responder } =
        request_stream.next().await.unwrap().unwrap();

    assert_matches!(options.initiator, Some(fidl_fuchsia_update::Initiator::User));

    assert_matches!(responder.send(), Ok(()));
    let mut monitor_stream = monitor.into_stream().unwrap();

    expect_states(
        &mut monitor_stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;

    // Send installer state and expect manager step in lockstep to make sure that event queue
    // won't merge any progress.
    sender.send(installer::State::Prepare).await.unwrap();
    expect_states(
        &mut monitor_stream,
        &[State::InstallingUpdate(InstallingData {
            update: update_info(),
            installation_progress: progress(Some(0.0)),
            ..InstallingData::EMPTY
        })],
    )
    .await;

    let installer_update_info = installer::UpdateInfo::builder().download_size(1000).build();
    sender
        .send(installer::State::Fetch(
            installer::UpdateInfoAndProgress::new(
                installer_update_info,
                installer::Progress::none(),
            )
            .unwrap(),
        ))
        .await
        .unwrap();
    expect_states(
        &mut monitor_stream,
        &[State::InstallingUpdate(InstallingData {
            update: update_info(),
            installation_progress: progress(Some(0.0)),
            ..InstallingData::EMPTY
        })],
    )
    .await;

    sender
        .send(installer::State::Stage(
            installer::UpdateInfoAndProgress::new(
                installer_update_info,
                installer::Progress::builder()
                    .fraction_completed(0.5)
                    .bytes_downloaded(500)
                    .build(),
            )
            .unwrap(),
        ))
        .await
        .unwrap();
    expect_states(
        &mut monitor_stream,
        &[State::InstallingUpdate(InstallingData {
            update: update_info(),
            installation_progress: progress(Some(0.5)),
            ..InstallingData::EMPTY
        })],
    )
    .await;

    sender
        .send(installer::State::WaitToReboot(installer::UpdateInfoAndProgress::done(
            installer_update_info,
        )))
        .await
        .unwrap();
    expect_states(
        &mut monitor_stream,
        &[
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(Some(1.0)),
                ..InstallingData::EMPTY
            }),
            State::WaitingForReboot(InstallingData {
                update: update_info(),
                installation_progress: progress(Some(1.0)),
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_progress_with_mock_installer() {
    let (mut sender, receiver) = mpsc::channel(0);
    let installer = MockUpdateInstallerService::builder().states_receiver(receiver).build();
    let env = TestEnvBuilder::new()
        .default_with_response(OmahaResponse::Update)
        .installer(installer)
        .build()
        .await;

    let mut stream = env.check_now().await;

    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;

    // Send installer state and expect manager step in lockstep to make sure that event queue
    // won't merge any progress.
    sender.send(installer::State::Prepare).await.unwrap();
    expect_states(
        &mut stream,
        &[State::InstallingUpdate(InstallingData {
            update: update_info(),
            installation_progress: progress(Some(0.0)),
            ..InstallingData::EMPTY
        })],
    )
    .await;

    let installer_update_info = installer::UpdateInfo::builder().download_size(1000).build();
    sender
        .send(installer::State::Fetch(
            installer::UpdateInfoAndProgress::new(
                installer_update_info,
                installer::Progress::none(),
            )
            .unwrap(),
        ))
        .await
        .unwrap();
    expect_states(
        &mut stream,
        &[State::InstallingUpdate(InstallingData {
            update: update_info(),
            installation_progress: progress(Some(0.0)),
            ..InstallingData::EMPTY
        })],
    )
    .await;

    sender
        .send(installer::State::Stage(
            installer::UpdateInfoAndProgress::new(
                installer_update_info,
                installer::Progress::builder()
                    .fraction_completed(0.5)
                    .bytes_downloaded(500)
                    .build(),
            )
            .unwrap(),
        ))
        .await
        .unwrap();
    expect_states(
        &mut stream,
        &[State::InstallingUpdate(InstallingData {
            update: update_info(),
            installation_progress: progress(Some(0.5)),
            ..InstallingData::EMPTY
        })],
    )
    .await;

    sender
        .send(installer::State::WaitToReboot(installer::UpdateInfoAndProgress::done(
            installer_update_info,
        )))
        .await
        .unwrap();
    expect_states(
        &mut stream,
        &[
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(Some(1.0)),
                ..InstallingData::EMPTY
            }),
            State::WaitingForReboot(InstallingData {
                update: update_info(),
                installation_progress: progress(Some(1.0)),
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_installation_deferred() {
    let (throttle_hook, throttler) = mphooks::throttle();
    let config_status_response = Arc::new(Mutex::new(Some(paver::ConfigurationStatus::Pending)));
    let env = {
        let config_status_response = Arc::clone(&config_status_response);
        TestEnvBuilder::new()
            .paver(
                MockPaverServiceBuilder::new()
                    .insert_hook(throttle_hook)
                    .insert_hook(mphooks::config_status(move |_| {
                        Ok(config_status_response.lock().as_ref().unwrap().clone())
                    }))
                    .build(),
            )
            .default_with_response(OmahaResponse::Update)
            .build()
            .await
    };

    // Allow the paver to emit enough events to unblock the CommitStatusProvider FIDL server, but
    // few enough to guarantee the commit is still pending.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::QueryCurrentConfiguration,
        PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A },
    ]);

    // The update attempt should start, but the install should be deferred b/c we're pending commit.
    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallationDeferredByPolicy(InstallationDeferredData {
                update: update_info(),
                deferral_reason: Some(InstallationDeferralReason::CurrentSystemNotCommitted),
                ..InstallationDeferredData::EMPTY
            }),
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);
    env.assert_platform_metrics(tree_assertion!(
        "children": {
            "0": contains {
                "event": "CheckingForUpdates",
            },
            "1": contains {
                "event": "InstallationDeferredByPolicy",
            },
        }
    ))
    .await;

    // Unblock any subsequent paver requests so that the system can commit.
    drop(throttler);

    // Wait for system to commit.
    let event_pair =
        env.proxies.commit_status_provider.is_current_system_committed().await.unwrap();
    assert_eq!(
        fasync::OnSignals::new(&event_pair, zx::Signals::USER_0).await,
        Ok(zx::Signals::USER_0)
    );

    // Now that the system is committed, we should be able to perform an update. Before we do the
    // update, make sure QueryConfigurationStatus returns Healthy. Otherwise, the update will fail
    // because the system-updater enforces the current slot is Healthy before applying an update.
    assert_eq!(
        config_status_response.lock().replace(paver::ConfigurationStatus::Healthy).unwrap(),
        paver::ConfigurationStatus::Pending
    );
    env.server.lock().set_all_cohort_assertions(Some("1:1:".to_string()));
    omaha_client_update(
        env,
        tree_assertion!(
            "children": {
                "0": contains {
                    "event": "CheckingForUpdates",
                },
                "1": contains {
                    "event": "InstallationDeferredByPolicy",
                },
                "2": contains {
                    "event": "CheckingForUpdates",
                },
                "3": contains {
                    "event": "InstallingUpdate",
                    "target-version": "0.1.2.3",
                },
                "4": contains {
                    "event": "WaitingForReboot",
                    "target-version": "0.1.2.3",
                }
            }
        ),
        true,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_error() {
    let env = TestEnvBuilder::new().default_with_response(OmahaResponse::Update).build().await;

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;
    let mut last_progress: Option<InstallationProgress> = None;
    let mut installation_error = false;
    while let Some(request) = stream.try_next().await.unwrap() {
        let MonitorRequest::OnState { state, responder } = request;
        match state {
            State::InstallingUpdate(InstallingData { update, installation_progress, .. }) => {
                assert_eq!(update, update_info());
                assert!(!installation_error);
                if let Some(last_progress) = last_progress {
                    let last = last_progress.fraction_completed.unwrap();
                    let current =
                        installation_progress.as_ref().unwrap().fraction_completed.unwrap();
                    assert!(
                        last <= current,
                        "progress is not increasing, last: {}, current: {}",
                        last,
                        current,
                    );
                }
                last_progress = installation_progress;
            }

            State::InstallationError(InstallationErrorData {
                update,
                installation_progress: _,
                ..
            }) => {
                assert_eq!(update, update_info());
                assert!(!installation_error);
                installation_error = true;
            }
            state => {
                panic!("Unexpected state: {:?}", state);
            }
        }
        responder.send().unwrap();
    }
    assert!(installation_error);

    env.assert_platform_metrics(tree_assertion!(
        "children": {
            "0": contains {
                "event": "CheckingForUpdates",
            },
            "1": contains {
                "event": "InstallingUpdate",
                "target-version": "0.1.2.3",
            },
            "2": contains {
                "event": "InstallationError",
                "target-version": "0.1.2.3",
            }
        }
    ))
    .await;

    assert_data_tree!(
        env.inspect_hierarchy().await,
        "root": contains {
            "platform_metrics": contains {
                "installation_error_events": contains {
                    "capacity": 50u64,
                    "children": {
                        "0": contains {
                            "event": "InstallationError",
                            "target-version": "0.1.2.3",
                            "ts": AnyProperty,
                        }
                    }

                }
            }
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_no_update() {
    let env = TestEnvBuilder::new().build().await;

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::NoUpdateAvailable(NoUpdateAvailableData::EMPTY),
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);

    env.assert_platform_metrics(tree_assertion!(
        "children": {
            "0": contains {
                "event": "CheckingForUpdates",
            },
            "1": contains {
                "event": "NoUpdateAvailable",
            },
        }
    ))
    .await;
}

async fn do_failed_update_check(env: &TestEnv) {
    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData::EMPTY),
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);
}

async fn do_nop_update_check(env: &TestEnv) {
    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::NoUpdateAvailable(NoUpdateAvailableData::EMPTY),
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_invalid_response() {
    let env =
        TestEnvBuilder::new().default_with_response(OmahaResponse::InvalidResponse).build().await;

    do_failed_update_check(&env).await;

    env.assert_platform_metrics(tree_assertion!(
        "children": {
            "0": contains {
                "event": "CheckingForUpdates",
            },
            "1": contains {
                "event": "ErrorCheckingForUpdate",
            }
        }
    ))
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_invalid_url() {
    let env = TestEnvBuilder::new().default_with_response(OmahaResponse::InvalidURL).build().await;

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallingData::EMPTY
            }),
            State::InstallationError(InstallationErrorData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallationErrorData::EMPTY
            }),
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);

    env.assert_platform_metrics(tree_assertion!(
        "children": {
            "0": contains {
                "event": "CheckingForUpdates",
            },
            "1": contains {
                "event": "InstallingUpdate",
                "target-version": "0.1.2.3",
            },
            "2": contains {
                "event": "InstallationError",
                "target-version": "0.1.2.3",
            }
        }
    ))
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_invalid_app_set() {
    let env = TestEnvBuilder::new().version("invalid-version").build().await;

    let options = CheckOptions {
        initiator: Some(Initiator::User),
        allow_attaching_to_existing_update_check: None,
        ..CheckOptions::EMPTY
    };
    assert_matches!(
        env.proxies.update_manager.check_now(options, None).await.expect("check_now"),
        Err(CheckNotStartedReason::Internal)
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_policy_config_inspect() {
    let env = TestEnvBuilder::new()
        .omaha_client_override_config_bool("allow_reboot_when_idle".into(), false)
        .omaha_client_override_config_uint16("startup_delay_seconds".into(), 61u16)
        .build()
        .await;

    // Wait for omaha client to start.
    let _ = env.proxies.channel_control.get_current().await;

    assert_data_tree!(
        env.inspect_hierarchy().await,
        "root": contains {
            "policy_config": {
                "periodic_interval": 60 * 60u64,
                "startup_delay": 61u64,
                "retry_delay": 5 * 60u64,
                "allow_reboot_when_idle": false,
            }
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_perform_pending_reboot_after_out_of_space() {
    let env = TestEnvBuilder::new().default_with_response(OmahaResponse::Update).build().await;

    // We should be able to get the update package just fine
    env.proxies
        .resolver
        .url("fuchsia-pkg://integration.test.fuchsia.com/update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
        .resolve(
        &env.proxies
            .resolver
            .package("update", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
            .add_file(
                "packages.json",
                make_packages_json(["fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead"]),
            )
            .add_file("zbi", "fake zbi")
            .add_file("epoch.json", make_current_epoch_json()),
    );

    // ...but the system image package should fail with NO_SPACE
    env.proxies
        .resolver.url("fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead")
        .fail(fidl_fuchsia_pkg::ResolveError::NoSpace);

    let mut stream = env.check_now().await;

    // Consume the initial states
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;

    // Monitor the installation until we get an installation error.
    let mut installation_error = false;
    while let Some(request) = stream.try_next().await.unwrap() {
        let MonitorRequest::OnState { state, responder } = request;
        match state {
            State::InstallingUpdate(InstallingData { update, .. }) => {
                assert_eq!(update, update_info());
            }
            State::InstallationError(InstallationErrorData { update, .. }) => {
                assert_eq!(update, update_info());
                assert!(!installation_error);
                installation_error = true;
            }
            state => {
                panic!("Unexpected state: {:?}", state);
            }
        }
        responder.send().unwrap();
    }
    assert!(installation_error);

    // Simulate an incoming call to PerformPendingReboot. It returns true if we're rebooting.
    assert!(env.perform_pending_reboot().await);

    // This will hang if reboot was not triggered.
    env.reboot_called.await.unwrap();
}

/// Verifies the signature of the CrashReport is what's expected.
fn assert_signature(report: CrashReport, expected_signature: &str) {
    assert_matches::assert_matches!(
        report,
        CrashReport {
            crash_signature: Some(signature),
            program_name: Some(program),
            program_uptime: Some(_),
            is_fatal: Some(false),
            ..
        } if signature == expected_signature && program == "system"
    )
}

/// When we fail with an installation error, we should file a crash report.
#[fasync::run_singlethreaded(test)]
async fn test_crash_report_installation_error() {
    let (hook, mut recv) = ThrottleHook::new(Ok(()));
    let env = TestEnvBuilder::new()
        .default_with_response(OmahaResponse::Update)
        .installer(MockUpdateInstallerService::with_response(Err(
            UpdateNotStartedReason::AlreadyInProgress,
        )))
        .crash_reporter(MockCrashReporterService::new(hook))
        .build()
        .await;

    let mut stream = env.check_now().await;

    // Consume states to get InstallationError.
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallingData::EMPTY
            }),
            State::InstallationError(InstallationErrorData {
                update: update_info(),
                installation_progress: progress(None),
                ..InstallationErrorData::EMPTY
            }),
        ],
    )
    .await;

    // Observe the crash report was filed.
    assert_signature(recv.next().await.unwrap(), "fuchsia-installation-error");
}

/// When we fail 5 times to check for updates, we should file a crash report.
#[fasync::run_singlethreaded(test)]
async fn test_crash_report_consecutive_failed_update_checks() {
    let (hook, mut recv) = ThrottleHook::new(Ok(()));
    let env = TestEnvBuilder::new()
        .default_with_response(OmahaResponse::InvalidResponse)
        .crash_reporter(MockCrashReporterService::new(hook))
        .build()
        .await;

    // Failing <5 times will not yield crash reports.
    do_failed_update_check(&env).await;
    do_failed_update_check(&env).await;
    do_failed_update_check(&env).await;
    do_failed_update_check(&env).await;
    assert_matches!(recv.try_next(), Err(_));

    // But failing >=5 times will.
    do_failed_update_check(&env).await;
    assert_signature(recv.next().await.unwrap(), "fuchsia-5-consecutive-failed-update-checks");
    do_failed_update_check(&env).await;
    assert_signature(recv.next().await.unwrap(), "fuchsia-6-consecutive-failed-update-checks");
}

#[fasync::run_singlethreaded(test)]
async fn test_update_check_sets_updatedisabled_when_opted_out() {
    use mock_omaha_server::UpdateCheckAssertion;

    let env = TestEnvBuilder::new().default_with_response(OmahaResponse::NoUpdate).build().await;

    // The default is to enable updates.
    env.server.lock().set_all_update_check_assertions(UpdateCheckAssertion::UpdatesEnabled);
    do_nop_update_check(&env).await;

    // The user preference is read for each update check.
    env.server.lock().set_all_update_check_assertions(UpdateCheckAssertion::UpdatesDisabled);
    env.proxies
        .config_optout
        .set(fuchsia_update_config_optout::OptOutPreference::AllowOnlySecurityUpdates);
    env.server.lock().set_all_cohort_assertions(Some("1:1:".to_string()));
    do_nop_update_check(&env).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_keeps_cohort() {
    let env = TestEnvBuilder::new().build().await;

    do_nop_update_check(&env).await;

    env.server.lock().set_all_cohort_assertions(Some("1:1:".to_string()));
    do_nop_update_check(&env).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_persists_cohort() {
    let mut env = TestEnvBuilder::new().build().await;

    do_nop_update_check(&env).await;

    // Stop omaha-client and restart it.
    let lifecycle_controller =
        connect_to_protocol::<fidl_fuchsia_sys2::LifecycleControllerMarker>().unwrap();
    lifecycle_controller
        .stop(
            &format!(
                "./realm_builder:{}/omaha_client_service",
                env.realm_instance.root.child_name()
            ),
            false,
        )
        .await
        .unwrap()
        .unwrap();
    env.proxies.update_manager = env
        .realm_instance
        .root
        .connect_to_protocol_at_exposed_dir::<ManagerMarker>()
        .expect("connect to update manager");

    env.server.lock().set_all_cohort_assertions(Some("1:1:".to_string()));
    do_nop_update_check(&env).await;
}
