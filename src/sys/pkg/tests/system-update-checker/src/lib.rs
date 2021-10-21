// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::anyhow,
    fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_paver::{self as paver, PaverRequestStream},
    fidl_fuchsia_update::{
        CheckOptions, CheckingForUpdatesData, CommitStatusProviderMarker,
        CommitStatusProviderProxy, Initiator, InstallationDeferralReason, InstallationDeferredData,
        InstallationProgress, InstallingData, ManagerMarker, ManagerProxy, MonitorMarker,
        MonitorRequest, MonitorRequestStream, State, UpdateInfo,
    },
    fidl_fuchsia_update_channel::{ProviderMarker, ProviderProxy},
    fidl_fuchsia_update_installer_ext as installer, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        RealmInstance,
    },
    fuchsia_pkg_testing::make_packages_json,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, prelude::*},
    matches::assert_matches,
    mock_installer::MockUpdateInstallerService,
    mock_paver::{hooks as mphooks, MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_resolver::MockResolverService,
    mock_verifier::MockVerifierService,
    parking_lot::Mutex,
    std::sync::Arc,
    tempfile::TempDir,
};

struct Mounts {
    misc_ota: TempDir,
    pkgfs_system: TempDir,
}

struct Proxies {
    resolver: Arc<MockResolverService>,
    channel_provider: ProviderProxy,
    update_manager: ManagerProxy,
    commit_status_provider: CommitStatusProviderProxy,
    _verifier: Arc<MockVerifierService>,
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

    async fn build(self) -> TestEnv {
        let mounts = Mounts::new();
        std::fs::write(
            mounts.pkgfs_system.path().join("meta"),
            "0000000000000000000000000000000000000000000000000000000000000001",
        )
        .expect("write pkgfs/system/meta");

        let mut fs = ServiceFs::new();
        // Add fake directories.
        let misc = io_util::directory::open_in_namespace(
            mounts.misc_ota.path().to_str().unwrap(),
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )
        .unwrap();
        let pkgfs_system = io_util::directory::open_in_namespace(
            mounts.pkgfs_system.path().to_str().unwrap(),
            io_util::OPEN_RIGHT_READABLE,
        )
        .unwrap();
        fs.dir("misc").add_remote("ota", misc);
        fs.dir("pkgfs").add_remote("system", pkgfs_system);

        // Setup the mock resolver service.
        let resolver = Arc::new(MockResolverService::new(None));
        let resolver_clone = Arc::clone(&resolver);
        fs.dir("svc").add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&resolver_clone)
                    .run_resolver_service(stream)
                    .unwrap_or_else(|e| panic!("error running resolver service {:?}", e)),
            )
            .detach()
        });

        // Setup the mock installer service.
        let installer = Arc::new(self.installer);
        let installer_clone = Arc::clone(&installer);
        fs.dir("svc").add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&installer_clone).run_service(stream)).detach()
        });
        // Setup the mock paver service
        let paver = Arc::new(self.paver.unwrap_or_else(|| MockPaverServiceBuilder::new().build()));
        fs.dir("svc").add_fidl_service(move |stream: PaverRequestStream| {
            fasync::Task::spawn(
                Arc::clone(&paver)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach();
        });
        // Setup the mock verifier service.
        let verifier = Arc::new(MockVerifierService::new(|_| Ok(())));
        let verifier_clone = Arc::clone(&verifier);
        fs.dir("svc").add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&verifier_clone).run_blobfs_verifier_service(stream))
                .detach()
        });

        let fs_holder = Mutex::new(Some(fs));
        let mut builder = RealmBuilder::new().await.expect("Failed to create test realm builder");
        builder
            .add_eager_component("system_update_checker",
                ComponentSource::url("fuchsia-pkg://fuchsia.com/system-update-checker-integration-tests#meta/system-update-checker.cm")).await.unwrap()
            .add_eager_component("system_update_committer",
                ComponentSource::url("fuchsia-pkg://fuchsia.com/system-update-checker-integration-tests#meta/system-update-committer.cm")).await.unwrap()
            .add_component("fake_capabilities", ComponentSource::mock(move |mock_handles| {
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
                    RouteEndpoint::component("system_update_checker"),
                    RouteEndpoint::component("system_update_committer"),
                ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.paver.Paver"),
                source: RouteEndpoint::component("fake_capabilities"),
                targets: vec![
                    RouteEndpoint::component("system_update_checker"),
                    RouteEndpoint::component("system_update_committer"),
                ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.update.installer.Installer"),
                source: RouteEndpoint::component("fake_capabilities"),
                targets: vec![ RouteEndpoint::component("system_update_checker") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.pkg.PackageResolver"),
                source: RouteEndpoint::component("fake_capabilities"),
                targets: vec![ RouteEndpoint::component("system_update_checker")],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.pkg.rewrite.Engine"),
                source: RouteEndpoint::component("fake_capabilities"),
                targets: vec![ RouteEndpoint::component("system_update_checker") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.pkg.RepositoryManager"),
                source: RouteEndpoint::component("fake_capabilities"),
                targets: vec![ RouteEndpoint::component("system_update_checker") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::directory("pkgfs-system", "/pkgfs/system", fio2::R_STAR_DIR),
                source: RouteEndpoint::component("fake_capabilities"),
                targets: vec![ RouteEndpoint::component("system_update_checker") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::directory("deprecated-misc-storage", "/misc", fio2::RW_STAR_DIR),
                source: RouteEndpoint::component("fake_capabilities"),
                targets: vec![ RouteEndpoint::component("system_update_checker") ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.update.channel.Provider"),
                source: RouteEndpoint::component("system_update_checker"),
                targets: vec! [ RouteEndpoint::AboveRoot ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.update.channelcontrol.ChannelControl"),
                source: RouteEndpoint::component("system_update_checker"),
                targets: vec! [ RouteEndpoint::AboveRoot ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.update.Manager"),
                source: RouteEndpoint::component("system_update_checker"),
                targets: vec! [ RouteEndpoint::AboveRoot ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.update.CommitStatusProvider"),
                source: RouteEndpoint::component("system_update_committer"),
                targets: vec![
                        RouteEndpoint::component("system_update_checker"),
                        RouteEndpoint::AboveRoot,
                ],
            }).unwrap();

        let realm_instance = builder.build().create().await.unwrap();
        let channel_provider = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<ProviderMarker>()
            .expect("connect to channel provider");
        let update_manager = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<ManagerMarker>()
            .expect("connect to update manager");
        let commit_status_provider = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<CommitStatusProviderMarker>()
            .expect("connect to commit status provider");

        TestEnv {
            _realm_instance: realm_instance,
            _mounts: mounts,
            proxies: Proxies {
                resolver,
                channel_provider,
                update_manager,
                commit_status_provider,
                _verifier: verifier,
            },
        }
    }
}

struct TestEnv {
    _realm_instance: RealmInstance,
    _mounts: Mounts,
    proxies: Proxies,
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
    let env = TestEnvBuilder::new().build().await;

    assert_eq!(
        env.proxies.channel_provider.get_current().await.expect("get_current"),
        "".to_string()
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_update_manager_check_now_error_checking_for_update() {
    let env = TestEnvBuilder::new().build().await;

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

    let env = TestEnvBuilder::new().installer(installer).build().await;

    env.proxies.resolver.url("fuchsia-pkg://fuchsia.com/update").resolve(
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
                .insert_hook(throttle_hook)
                .insert_hook(mphooks::config_status(move |_| {
                    Ok(config_status_response_clone.lock().as_ref().unwrap().clone())
                }))
                .build(),
        )
        .build()
        .await;

    env.proxies.resolver.url("fuchsia-pkg://fuchsia.com/update").resolve(
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
