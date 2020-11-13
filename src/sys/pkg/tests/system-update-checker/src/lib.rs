// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fidl_fuchsia_paver::{Configuration, PaverRequestStream},
    fidl_fuchsia_update::{
        CheckOptions, CheckingForUpdatesData, Initiator, InstallationProgress, InstallingData,
        ManagerMarker, ManagerProxy, MonitorMarker, MonitorRequest, MonitorRequestStream, State,
        UpdateInfo,
    },
    fidl_fuchsia_update_channel::{ProviderMarker, ProviderProxy},
    fidl_fuchsia_update_installer_ext as installer, fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_pkg_testing::make_packages_json,
    fuchsia_zircon::Status,
    futures::{channel::mpsc, prelude::*},
    mock_installer::MockUpdateInstallerService,
    mock_paver::{hooks as mphooks, MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_resolver::MockResolverService,
    std::{fs::File, sync::Arc},
    tempfile::TempDir,
};

const SYSTEM_UPDATE_CHECKER_CMX: &str = "fuchsia-pkg://fuchsia.com/system-update-checker-integration-tests#meta/system-update-checker-for-integration-test.cmx";

struct Mounts {
    misc_ota: TempDir,
    pkgfs_system: TempDir,
}

struct Proxies {
    _paver: Arc<MockPaverService>,
    paver_events: mpsc::UnboundedReceiver<PaverEvent>,
    resolver: Arc<MockResolverService>,
    channel_provider: ProviderProxy,
    update_manager: ManagerProxy,
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
    paver_builder: MockPaverServiceBuilder,
    paver_events: mpsc::UnboundedReceiver<PaverEvent>,
    installer: MockUpdateInstallerService,
}

impl TestEnvBuilder {
    fn new() -> Self {
        let (events_tx, events_rx) = mpsc::unbounded();
        let paver_builder = MockPaverServiceBuilder::new().event_hook(move |event| {
            events_tx.unbounded_send(event.to_owned()).expect("to write to events channel")
        });
        Self {
            paver_builder,
            paver_events: events_rx,
            installer: MockUpdateInstallerService::builder().build(),
        }
    }

    fn paver_init<F>(self, paver_init: F) -> Self
    where
        F: FnOnce(MockPaverServiceBuilder) -> MockPaverServiceBuilder,
    {
        Self { paver_builder: paver_init(self.paver_builder), ..self }
    }

    fn installer(self, installer: MockUpdateInstallerService) -> Self {
        Self { installer, ..self }
    }

    fn build(self) -> TestEnv {
        let mounts = Mounts::new();
        std::fs::write(
            mounts.pkgfs_system.path().join("meta"),
            "0000000000000000000000000000000000000000000000000000000000000001",
        )
        .expect("write pkgfs/system/meta");

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();

        let paver = self.paver_builder.build();
        let paver = Arc::new(paver);
        let paver_clone = Arc::clone(&paver);
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            fasync::Task::spawn(
                Arc::clone(&paver_clone)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("Failed to run paver: {:?}", e)),
            )
            .detach();
        });

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

        let env = fs
            .create_salted_nested_environment("system-update-checker_integration_test_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

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
                _paver: paver,
                paver_events: self.paver_events,
                resolver,
                channel_provider: system_update_checker
                    .connect_to_service::<ProviderMarker>()
                    .expect("connect to channel provider"),
                update_manager: system_update_checker
                    .connect_to_service::<ManagerMarker>()
                    .expect("connect to update manager"),
            },
            _system_update_checker: system_update_checker,
        }
    }
}

struct TestEnv {
    _env: NestedEnvironment,
    _mounts: Mounts,
    proxies: Proxies,
    _system_update_checker: App,
}

impl TestEnv {
    async fn check_now(&self) -> MonitorRequestStream {
        let options = CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(false),
            ..CheckOptions::empty()
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
        ..UpdateInfo::empty()
    })
}

fn progress(fraction_completed: Option<f32>) -> Option<InstallationProgress> {
    Some(InstallationProgress { fraction_completed, ..InstallationProgress::empty() })
}

#[fasync::run_singlethreaded(test)]
// Test will hang if system-update-checker does not call paver service
async fn test_calls_paver_service() {
    let env = TestEnvBuilder::new().build();

    assert_eq!(
        env.proxies.paver_events.take(2).collect::<Vec<PaverEvent>>().await,
        vec![
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::QueryConfigurationStatus { configuration: Configuration::A }
        ]
    );
}

#[fasync::run_singlethreaded(test)]
// Test will hang if system-update-checker does not call paver service
async fn test_channel_provider_get_current_works_after_paver_service_fails() {
    let env = TestEnvBuilder::new()
        .paver_init(|p| p.insert_hook(mphooks::return_error(|_| Status::INTERNAL)))
        .build();

    assert_eq!(
        env.proxies.paver_events.take(1).collect::<Vec<PaverEvent>>().await,
        vec![PaverEvent::QueryCurrentConfiguration]
    );

    assert_eq!(
        env.proxies.channel_provider.get_current().await.expect("get_current"),
        "".to_string()
    );
}

#[fasync::run_singlethreaded(test)]
// Test will hang if system-update-checker does not call paver service
async fn test_update_manager_check_now_works_after_paver_service_fails() {
    let env = TestEnvBuilder::new()
        .paver_init(|p| p.insert_hook(mphooks::return_error(|_| Status::INTERNAL)))
        .build();

    assert_eq!(
        env.proxies.paver_events.take(1).collect::<Vec<PaverEvent>>().await,
        vec![PaverEvent::QueryCurrentConfiguration]
    );

    let (client_end, request_stream) =
        fidl::endpoints::create_request_stream().expect("create_request_stream");

    assert!(env
        .proxies
        .update_manager
        .check_now(
            fidl_fuchsia_update::CheckOptions {
                initiator: Some(fidl_fuchsia_update::Initiator::User),
                allow_attaching_to_existing_update_check: Some(true),
                ..fidl_fuchsia_update::CheckOptions::empty()
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
            State::CheckingForUpdates(fidl_fuchsia_update::CheckingForUpdatesData::empty()),
            State::ErrorCheckingForUpdate(fidl_fuchsia_update::ErrorCheckingForUpdateData::empty()),
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
            State::CheckingForUpdates(CheckingForUpdatesData::empty()),
            State::InstallingUpdate(InstallingData {
                update: update_info(None),
                installation_progress: None,
                ..InstallingData::empty()
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
            ..InstallingData::empty()
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
            ..InstallingData::empty()
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
            ..InstallingData::empty()
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
            ..InstallingData::empty()
        })],
    )
    .await;
}
