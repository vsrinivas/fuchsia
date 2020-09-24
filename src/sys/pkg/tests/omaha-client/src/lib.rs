// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::anyhow,
    fidl_fuchsia_paver::PaverRequestStream,
    fidl_fuchsia_pkg::{PackageCacheRequestStream, PackageResolverRequestStream},
    fidl_fuchsia_update::{
        CheckNotStartedReason, CheckOptions, CheckingForUpdatesData, ErrorCheckingForUpdateData,
        Initiator, InstallationErrorData, InstallationProgress, InstallingData, ManagerMarker,
        ManagerProxy, MonitorMarker, MonitorRequest, MonitorRequestStream, NoUpdateAvailableData,
        State, UpdateInfo,
    },
    fidl_fuchsia_update_channelcontrol::{ChannelControlMarker, ChannelControlProxy},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_inspect::{
        assert_inspect_tree, reader::NodeHierarchy, testing::TreeAssertion, tree_assertion,
    },
    fuchsia_pkg_testing::get_inspect_hierarchy,
    fuchsia_zircon::{self as zx, Status},
    futures::{channel::oneshot, prelude::*},
    matches::assert_matches,
    mock_omaha_server::{OmahaResponse, OmahaServer},
    mock_paver::{MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_resolver::MockResolverService,
    parking_lot::Mutex,
    std::{
        fs::{self, create_dir, File},
        path::PathBuf,
        sync::Arc,
    },
    tempfile::TempDir,
};

const OMAHA_CLIENT_CMX: &str =
    "fuchsia-pkg://fuchsia.com/omaha-client-integration-tests#meta/omaha-client-service-for-integration-test.cmx";

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
}
struct Proxies {
    _cache: Arc<MockCache>,
    resolver: Arc<MockResolverService>,
    update_manager: ManagerProxy,
    channel_control: ChannelControlProxy,
}

struct TestEnvBuilder {
    paver: MockPaverService,
    response: OmahaResponse,
    version: String,
}

impl TestEnvBuilder {
    fn new() -> Self {
        Self {
            paver: MockPaverServiceBuilder::new().build(),
            response: OmahaResponse::NoUpdate,
            version: "0.1.2.3".to_string(),
        }
    }

    fn paver(self, paver: MockPaverService) -> Self {
        Self { paver, response: self.response, version: self.version }
    }

    fn response(self, response: OmahaResponse) -> Self {
        Self { paver: self.paver, response, version: self.version }
    }

    fn version(self, version: impl Into<String>) -> Self {
        Self { paver: self.paver, response: self.response, version: version.into() }
    }

    fn build(self) -> TestEnv {
        let mounts = Mounts::new();

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>()
            .add_proxy_service::<fidl_fuchsia_posix_socket::ProviderMarker, _>();

        let server = OmahaServer::new(self.response);
        let url = server.start().expect("start server");
        mounts.write_url(url);
        mounts.write_appid("integration-test-appid");
        mounts.write_version(self.version);

        let paver = Arc::new(self.paver);
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            fasync::Task::spawn(
                Arc::clone(&paver)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach();
        });

        let resolver = Arc::new(MockResolverService::new(None));
        let resolver_clone = resolver.clone();
        fs.add_fidl_service(move |stream: PackageResolverRequestStream| {
            let resolver_clone = resolver_clone.clone();
            fasync::Task::spawn(
                resolver_clone
                    .run_resolver_service(stream)
                    .unwrap_or_else(|e| panic!("error running resolver service {:?}", e)),
            )
            .detach()
        });

        let cache = Arc::new(MockCache::new());
        let cache_clone = cache.clone();
        fs.add_fidl_service(move |stream: PackageCacheRequestStream| {
            let cache_clone = cache_clone.clone();
            fasync::Task::spawn(cache_clone.run_cache_service(stream)).detach()
        });

        let nested_environment_label = Self::make_nested_environment_label();
        let env = fs
            .create_nested_environment(&nested_environment_label)
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        let omaha_client = AppBuilder::new(OMAHA_CLIENT_CMX)
            .add_dir_to_namespace(
                "/config/data".into(),
                File::open(&mounts.config_data).expect("open config_data"),
            )
            .unwrap()
            .add_dir_to_namespace(
                "/config/build-info".into(),
                File::open(&mounts.build_info).expect("open build_info"),
            )
            .unwrap()
            .spawn(env.launcher())
            .expect("omaha_client to launch");

        TestEnv {
            _env: env,
            _mounts: mounts,
            proxies: Proxies {
                _cache: cache,
                resolver,
                update_manager: omaha_client
                    .connect_to_service::<ManagerMarker>()
                    .expect("connect to update manager"),
                channel_control: omaha_client
                    .connect_to_service::<ChannelControlMarker>()
                    .expect("connect to channel control"),
            },
            _omaha_client: omaha_client,
            nested_environment_label,
        }
    }

    fn make_nested_environment_label() -> String {
        let mut salt = [0; 4];
        zx::cprng_draw(&mut salt[..]).expect("zx_cprng_draw does not fail");
        // omaha_client_integration_test_env_xxxxxxxx is too long and gets truncated.
        format!("omaha_client_test_env_{}", hex::encode(&salt))
    }
}

struct TestEnv {
    _env: NestedEnvironment,
    _mounts: Mounts,
    proxies: Proxies,
    _omaha_client: App,
    nested_environment_label: String,
}

impl TestEnv {
    async fn check_now(&self) -> MonitorRequestStream {
        for _ in 0..20u8 {
            let options = CheckOptions {
                initiator: Some(Initiator::User),
                allow_attaching_to_existing_update_check: Some(false),
            };
            let (client_end, stream) =
                fidl::endpoints::create_request_stream::<MonitorMarker>().unwrap();
            match self
                .proxies
                .update_manager
                .check_now(options, Some(client_end))
                .await
                .expect("check_now")
            {
                Ok(()) => {
                    return stream;
                }
                Err(CheckNotStartedReason::AlreadyInProgress) => {
                    println!("Update already in progress, waiting 1s...");
                    fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(1))).await;
                }
                Err(e) => {
                    panic!("Unexpected check_now error: {:?}", e);
                }
            }
        }
        panic!("Timeout waiting to start update check");
    }

    async fn inspect_hierarchy(&self) -> NodeHierarchy {
        get_inspect_hierarchy(
            &self.nested_environment_label,
            "omaha-client-service-for-integration-test.cmx",
        )
        .await
    }

    async fn assert_platform_metrics(&self, children: TreeAssertion) {
        assert_inspect_tree!(
            self.inspect_hierarchy().await,
            "root": contains {
                "platform_metrics": {
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
        Self {}
    }
    async fn run_cache_service(self: Arc<Self>, mut stream: PackageCacheRequestStream) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl_fuchsia_pkg::PackageCacheRequest::Sync { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("unsupported PackageCache request: {:?}", other),
            }
        }
    }
}

// Test will hang if omaha-client does not call set_active_configuration_healthy on the paver
// service.
#[fasync::run_singlethreaded(test)]
async fn test_calls_set_active_configuration_healthy() {
    let (send, recv) = oneshot::channel();
    let send = Mutex::new(Some(send));
    let paver = MockPaverServiceBuilder::new()
        .call_hook(move |event| {
            match event {
                PaverEvent::SetActiveConfigurationHealthy => {
                    send.lock().take().unwrap().send(()).unwrap();
                }
                _ => {}
            }
            Status::OK
        })
        .build();
    let _env = TestEnvBuilder::new().paver(paver).build();

    // wait for the call hook to notify `send`.
    let () = recv.await.unwrap();
}

// Test will hang if omaha-client does not call set_active_configuration_healthy on the paver
// service.
#[fasync::run_singlethreaded(test)]
async fn test_update_manager_checknow_works_after_set_active_configuration_healthy_fails() {
    let (send, recv) = oneshot::channel();
    let send = Mutex::new(Some(send));
    let paver = MockPaverServiceBuilder::new()
        .call_hook(move |event| match event {
            PaverEvent::SetActiveConfigurationHealthy => {
                send.lock().take().unwrap().send(()).unwrap();
                Status::INTERNAL
            }
            _ => Status::OK,
        })
        .build();
    let env = TestEnvBuilder::new().paver(paver).build();

    // wait for the call hook to notify `send`.
    let () = recv.await.unwrap();

    let mut stream = env.check_now().await;
    assert_matches!(stream.next().await, Some(_));
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
    Some(UpdateInfo { version_available: None, download_size: None })
}

fn progress(fraction_completed: Option<f32>) -> Option<InstallationProgress> {
    Some(InstallationProgress { fraction_completed })
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update() {
    let env = TestEnvBuilder::new().response(OmahaResponse::Update).build();

    env.proxies
        .resolver
        .url("fuchsia-pkg://integration.test.fuchsia.com/update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
        .resolve(
        &env.proxies
            .resolver
            .package("update", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
            .add_file(
                "packages",
                "system_image/0=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead\n",
            )
            .add_file("zbi", "fake zbi"),
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
            State::CheckingForUpdates(CheckingForUpdatesData {}),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
            }),
        ],
    )
    .await;
    let mut last_progress: Option<InstallationProgress> = None;
    let mut waiting_for_reboot = false;
    while let Some(request) = stream.try_next().await.unwrap() {
        let MonitorRequest::OnState { state, responder } = request;
        match state {
            State::InstallingUpdate(InstallingData { update, installation_progress }) => {
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
            State::WaitingForReboot(InstallingData { update, installation_progress }) => {
                assert_eq!(update, update_info());
                assert_eq!(installation_progress, progress(Some(1.)));
                assert!(!waiting_for_reboot);
                waiting_for_reboot = true;
            }
            state => {
                panic!("Unexpected state: {:?}", state);
            }
        }
        responder.send().unwrap();
    }
    assert_matches!(last_progress, Some(_));
    assert!(waiting_for_reboot);

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
                "event": "WaitingForReboot",
                "target-version": "0.1.2.3",
            }
        }
    ))
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_error() {
    let env = TestEnvBuilder::new().response(OmahaResponse::Update).build();

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData {}),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
            }),
        ],
    )
    .await;
    let mut last_progress: Option<InstallationProgress> = None;
    let mut installation_error = false;
    while let Some(request) = stream.try_next().await.unwrap() {
        let MonitorRequest::OnState { state, responder } = request;
        match state {
            State::InstallingUpdate(InstallingData { update, installation_progress }) => {
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
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_no_update() {
    let env = TestEnvBuilder::new().build();

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData {}),
            State::NoUpdateAvailable(NoUpdateAvailableData {}),
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

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_invalid_response() {
    let env = TestEnvBuilder::new().response(OmahaResponse::InvalidResponse).build();

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData {}),
            State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData {}),
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
                "event": "ErrorCheckingForUpdate",
            }
        }
    ))
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_invalid_url() {
    let env = TestEnvBuilder::new().response(OmahaResponse::InvalidURL).build();

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData {}),
            State::InstallingUpdate(InstallingData {
                update: update_info(),
                installation_progress: progress(None),
            }),
            State::InstallationError(InstallationErrorData {
                update: update_info(),
                installation_progress: progress(None),
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
    let env = TestEnvBuilder::new().version("invalid-version").build();

    let options = CheckOptions {
        initiator: Some(Initiator::User),
        allow_attaching_to_existing_update_check: None,
    };
    assert_matches!(
        env.proxies.update_manager.check_now(options, None).await.expect("check_now"),
        Err(CheckNotStartedReason::Internal)
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_policy_config_inspect() {
    let env = TestEnvBuilder::new().build();

    // Wait for omaha client to start.
    let _ = env.proxies.channel_control.get_current().await;

    assert_inspect_tree!(
        env.inspect_hierarchy().await,
        "root": contains {
            "policy_config": {
                "periodic_interval": 60 * 60u64,
                "startup_delay": 60u64,
                "retry_delay": 5 * 60u64,
                "allow_reboot_when_idle": true,
            }
        }
    );
}
