// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::anyhow,
    fidl_fuchsia_paver::{self as paver, PaverRequestStream},
    fidl_fuchsia_update::{
        CheckOptions, CheckingForUpdatesData, CommitStatusProviderMarker,
        CommitStatusProviderProxy, Initiator, InstallationDeferralReason, InstallationDeferredData,
        InstallationProgress, InstallingData, ManagerMarker, ManagerProxy, MonitorMarker,
        MonitorRequest, MonitorRequestStream, State, UpdateInfo,
    },
    fidl_fuchsia_update_channel::{ProviderMarker, ProviderProxy},
    fidl_fuchsia_update_installer_ext as installer, fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_pkg_testing::make_packages_json,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, prelude::*},
    matches::assert_matches,
    mock_installer::MockUpdateInstallerService,
    mock_paver::{hooks as mphooks, MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_resolver::MockResolverService,
    parking_lot::Mutex,
    std::{fs::File, sync::Arc},
    tempfile::TempDir,
};

const SYSTEM_UPDATE_CHECKER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/system-update-checker-integration-tests#meta/system-update-checker-for-integration-test.cmx";
const SYSTEM_UPDATE_COMMITTER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/system-update-checker-integration-tests#meta/system-update-committer.cmx";

struct Mounts {
    misc_ota: TempDir,
    pkgfs_system: TempDir,
}

struct Proxies {
    resolver: Arc<MockResolverService>,
    channel_provider: ProviderProxy,
    update_manager: ManagerProxy,
    commit_status_provider: CommitStatusProviderProxy,
}

impl Mounts {
    fn new() -> Self {
        Self {
            misc_ota: tempfile::tempdir().expect("/tmp to exist"),
            pkgfs_system: tempfile::tempdir().expect("/tmp to exist"),
        }
    }
}

struct TestEnvBuilder {
    installer: MockUpdateInstallerService,
    paver: Option<MockPaverService>,
}

impl TestEnvBuilder {
    fn new() -> Self {
        Self { installer: MockUpdateInstallerService::builder().build(), paver: None }
    }

    fn installer(self, installer: MockUpdateInstallerService) -> Self {
        Self { installer, ..self }
    }

    fn paver(self, paver: MockPaverService) -> Self {
        Self { paver: Some(paver), ..self }
    }

    fn build(self) -> TestEnv {
        let mounts = Mounts::new();
        std::fs::write(
            mounts.pkgfs_system.path().join("meta"),
            "0000000000000000000000000000000000000000000000000000000000000001",
        )
        .expect("write pkgfs/system/meta");

        let mut system_update_committer = AppBuilder::new(SYSTEM_UPDATE_COMMITTER_CMX.to_owned());

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>()
            .add_proxy_service_to::<fidl_fuchsia_update::CommitStatusProviderMarker, _>(
            system_update_committer.directory_request().unwrap().clone(),
        );

        let resolver = Arc::new(MockResolverService::new(None));
        let resolver_clone = Arc::clone(&resolver);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&resolver_clone)
                    .run_resolver_service(stream)
                    .unwrap_or_else(|e| panic!("error running resolver service {:?}", e)),
            )
            .detach()
        });

        let installer = Arc::new(self.installer);
        let installer_clone = Arc::clone(&installer);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&installer_clone).run_service(stream)).detach()
        });

        let paver = Arc::new(self.paver.unwrap_or_else(|| MockPaverServiceBuilder::new().build()));
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            fasync::Task::spawn(
                Arc::clone(&paver)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach();
        });

        let env = fs
            .create_salted_nested_environment("system-update-checker_integration_test_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        let system_update_committer = system_update_committer
            .spawn(env.launcher())
            .expect("system-update-committer to launch");

        let system_update_checker = AppBuilder::new(SYSTEM_UPDATE_CHECKER_CMX)
            .add_dir_to_namespace(
                "/misc/ota".to_string(),
                File::open(mounts.misc_ota.path()).expect("/misc/ota tempdir to open"),
            )
            .expect("/misc/ota to mount")
            .add_dir_to_namespace(
                "/pkgfs/system".to_string(),
                File::open(mounts.pkgfs_system.path()).expect("/pkgfs/system tempdir to open"),
            )
            .expect("/pkgfs/system to mount")
            .spawn(env.launcher())
            .expect("system_update_checker to launch");

        TestEnv {
            _env: env,
            _mounts: mounts,
            proxies: Proxies {
                resolver,
                channel_provider: system_update_checker
                    .connect_to_service::<ProviderMarker>()
                    .expect("connect to channel provider"),
                update_manager: system_update_checker
                    .connect_to_service::<ManagerMarker>()
                    .expect("connect to update manager"),
                commit_status_provider: system_update_committer
                    .connect_to_service::<CommitStatusProviderMarker>()
                    .expect("connect to commit status provider"),
            },
            _system_update_checker: system_update_checker,
            _system_update_committer: system_update_committer,
        }
    }
}

struct TestEnv {
    _env: NestedEnvironment,
    _mounts: Mounts,
    proxies: Proxies,
    _system_update_checker: App,
    _system_update_committer: App,
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
}

async fn expect_states(stream: &mut MonitorRequestStream, expected_states: &[State]) {
    for expected_state in expected_states {
        let MonitorRequest::OnState { state, responder } =
            stream.try_next().await.unwrap().unwrap();
        assert_eq!(&state, expected_state);
        responder.send().unwrap();
    }
}

fn update_info(download_size: Option<u64>) -> Option<UpdateInfo> {
    Some(UpdateInfo {
        version_available: Some(
            "beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead".to_string(),
        ),
        download_size,
        ..UpdateInfo::EMPTY
    })
}

fn progress(fraction_completed: Option<f32>) -> Option<InstallationProgress> {
    Some(InstallationProgress { fraction_completed, ..InstallationProgress::EMPTY })
}

#[fasync::run_singlethreaded(test)]
async fn test_channel_provider_get_current() {
    let env = TestEnvBuilder::new().build();

    assert_eq!(
        env.proxies.channel_provider.get_current().await.expect("get_current"),
        "".to_string()
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_update_manager_check_now_error_checking_for_update() {
    let env = TestEnvBuilder::new().build();

    let (client_end, request_stream) =
        fidl::endpoints::create_request_stream().expect("create_request_stream");

    assert!(env
        .proxies
        .update_manager
        .check_now(
            fidl_fuchsia_update::CheckOptions {
                initiator: Some(fidl_fuchsia_update::Initiator::User),
                allow_attaching_to_existing_update_check: Some(true),
                ..fidl_fuchsia_update::CheckOptions::EMPTY
            },
            Some(client_end),
        )
        .await
        .is_ok());
    assert_eq!(
        request_stream
            .map(|r| {
                let MonitorRequest::OnState { state, responder } = r.unwrap();
                responder.send().unwrap();
                state
            })
            .collect::<Vec<State>>()
            .await,
        vec![
            State::CheckingForUpdates(fidl_fuchsia_update::CheckingForUpdatesData::EMPTY),
            State::ErrorCheckingForUpdate(fidl_fuchsia_update::ErrorCheckingForUpdateData::EMPTY),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_update_manager_progress() {
    let (mut sender, receiver) = mpsc::channel(0);
    let installer = MockUpdateInstallerService::builder().states_receiver(receiver).build();

    let env = TestEnvBuilder::new().installer(installer).build();

    env.proxies.resolver.url("fuchsia-pkg://fuchsia.com/update/0").resolve(
        &env.proxies
            .resolver
            .package("update", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
            .add_file(
                "packages.json",
                make_packages_json(["fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead"]),
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
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(None),
                installation_progress: None,
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;
    sender.send(installer::State::Prepare).await.unwrap();
    expect_states(
        &mut stream,
        &[State::InstallingUpdate(InstallingData {
            update: update_info(None),
            installation_progress: progress(None),
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
            update: update_info(Some(1000)),
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
            update: update_info(Some(1000)),
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
        &[State::WaitingForReboot(InstallingData {
            update: update_info(Some(1000)),
            installation_progress: progress(Some(1.0)),
            ..InstallingData::EMPTY
        })],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_installation_deferred() {
    let (throttle_hook, throttler) = mphooks::throttle();
    let config_status_response = Arc::new(Mutex::new(Some(paver::ConfigurationStatus::Pending)));
    let config_status_response_clone = Arc::clone(&config_status_response);
    let env = TestEnvBuilder::new()
        .paver(
            MockPaverServiceBuilder::new()
                .insert_hook(mphooks::config_status(move |_| {
                    Ok(config_status_response_clone.lock().as_ref().unwrap().clone())
                }))
                .insert_hook(throttle_hook)
                .build(),
        )
        .build();

    env.proxies.resolver.url("fuchsia-pkg://fuchsia.com/update/0").resolve(
            &env.proxies
                .resolver
                .package("update", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")
                .add_file(
                    "packages.json",
                    make_packages_json(["fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead"]),
                )
                .add_file("zbi", "fake zbi"),
        );

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
                update: update_info(None),
                deferral_reason: Some(InstallationDeferralReason::CurrentSystemNotCommitted),
                ..InstallationDeferredData::EMPTY
            }),
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);

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

    stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            State::InstallingUpdate(InstallingData {
                update: update_info(None),
                installation_progress: None,
                ..InstallingData::EMPTY
            }),
        ],
    )
    .await;
}
