// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    assert_matches::assert_matches,
    diagnostics_hierarchy::DiagnosticsHierarchy,
    diagnostics_reader::{ArchiveReader, Inspect},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_paver::{Configuration, ConfigurationStatus},
    fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderProxy},
    fuchsia_async::{self as fasync, OnSignals, TimeoutExt},
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_inspect::{assert_data_tree, testing::AnyProperty},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{channel::oneshot, prelude::*},
    mock_paver::{hooks as mphooks, MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_reboot::{MockRebootService, RebootReason},
    mock_verifier::MockVerifierService,
    parking_lot::Mutex,
    serde_json::json,
    std::{path::PathBuf, sync::Arc, time::Duration},
    tempfile::TempDir,
};

const SYSTEM_UPDATE_COMMITTER_CM: &str =
    "fuchsia-pkg://fuchsia.com/system-update-committer-integration-tests#meta/system-update-committer.cm";
const HANG_DURATION: Duration = Duration::from_millis(500);

struct TestEnvBuilder {
    config_data: Option<(PathBuf, String)>,
    paver_service_builder: Option<MockPaverServiceBuilder>,
    reboot_service: Option<MockRebootService>,
    verifier_service: Option<MockVerifierService>,
}
impl TestEnvBuilder {
    fn config_data(self, path: impl Into<PathBuf>, data: impl Into<String>) -> Self {
        Self { config_data: Some((path.into(), data.into())), ..self }
    }

    fn paver_service_builder(self, paver_service_builder: MockPaverServiceBuilder) -> Self {
        Self { paver_service_builder: Some(paver_service_builder), ..self }
    }

    fn reboot_service(self, reboot_service: MockRebootService) -> Self {
        Self { reboot_service: Some(reboot_service), ..self }
    }

    fn verifier_service(self, verifier_service: MockVerifierService) -> Self {
        Self { verifier_service: Some(verifier_service), ..self }
    }

    async fn build(self) -> TestEnv {
        // Optionally write config data.
        let config_data = tempfile::tempdir().expect("/tmp to exist");
        if let Some((path, data)) = self.config_data {
            let path = config_data.path().join(path);
            assert!(!path.exists());
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, &data).unwrap();
        }

        let mut fs = ServiceFs::new();
        let config_data_proxy = fuchsia_fs::directory::open_in_namespace(
            config_data.path().to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        fs.dir("config").add_remote("data", config_data_proxy);

        let mut svc = fs.dir("svc");

        // Set up paver service.
        let paver_service_builder =
            self.paver_service_builder.unwrap_or_else(|| MockPaverServiceBuilder::new());
        let paver_service = Arc::new(paver_service_builder.build());
        {
            let paver_service = Arc::clone(&paver_service);
            svc.add_fidl_service(move |stream| {
                fasync::Task::spawn(
                    Arc::clone(&paver_service).run_paver_service(stream).unwrap_or_else(|e| {
                        panic!("error running paver service: {:#}", anyhow!(e))
                    }),
                )
                .detach()
            });
        }

        // Set up reboot service.
        let reboot_service = Arc::new(self.reboot_service.unwrap_or_else(|| {
            MockRebootService::new(Box::new(|_| panic!("unexpected call to reboot")))
        }));
        {
            let reboot_service = Arc::clone(&reboot_service);
            svc.add_fidl_service(move |stream| {
                fasync::Task::spawn(
                    Arc::clone(&reboot_service).run_reboot_service(stream).unwrap_or_else(|e| {
                        panic!("error running reboot service: {:#}", anyhow!(e))
                    }),
                )
                .detach()
            });
        }

        // Set up verifier service.
        let verifier_service =
            Arc::new(self.verifier_service.unwrap_or_else(|| MockVerifierService::new(|_| Ok(()))));
        {
            let verifier_service = Arc::clone(&verifier_service);
            svc.add_fidl_service(move |stream| {
                fasync::Task::spawn(
                    Arc::clone(&verifier_service).run_blobfs_verifier_service(stream),
                )
                .detach()
            });
        }

        let fs_holder = Mutex::new(Some(fs));
        let builder = RealmBuilder::new().await.expect("Failed to create test realm builder");
        let system_update_committer = builder
            .add_child(
                "system_update_committer",
                SYSTEM_UPDATE_COMMITTER_CM,
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
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&system_update_committer),
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
                    .capability(Capability::protocol_by_name(
                        "fuchsia.hardware.power.statecontrol.Admin",
                    ))
                    .capability(Capability::protocol_by_name("fuchsia.paver.Paver"))
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
                    .capability(Capability::protocol_by_name("fuchsia.update.CommitStatusProvider"))
                    .from(&system_update_committer)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        let realm_instance = builder.build().await.unwrap();
        let commit_status_provider = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<CommitStatusProviderMarker>()
            .expect("connect to commit status provider");

        TestEnv {
            _config_data: config_data,
            realm_instance,
            commit_status_provider,
            _paver_service: paver_service,
            _reboot_service: reboot_service,
            _verifier_service: verifier_service,
        }
    }
}
struct TestEnv {
    _config_data: TempDir,
    realm_instance: RealmInstance,
    commit_status_provider: CommitStatusProviderProxy,
    _paver_service: Arc<MockPaverService>,
    _reboot_service: Arc<MockRebootService>,
    _verifier_service: Arc<MockVerifierService>,
}

impl TestEnv {
    fn builder() -> TestEnvBuilder {
        TestEnvBuilder {
            config_data: None,
            paver_service_builder: None,
            reboot_service: None,
            verifier_service: None,
        }
    }

    /// Opens a connection to the fuchsia.update/CommitStatusProvider FIDL service.
    fn commit_status_provider_proxy(&self) -> CommitStatusProviderProxy {
        self.commit_status_provider.clone()
    }

    async fn system_update_committer_inspect_hierarchy(&self) -> DiagnosticsHierarchy {
        let mut data = ArchiveReader::new()
            .add_selector(format!(
                "realm_builder\\:{}/system_update_committer:root",
                self.realm_instance.root.child_name()
            ))
            .snapshot::<Inspect>()
            .await
            .expect("got inspect data");
        assert_eq!(data.len(), 1, "expected 1 match: {:?}", data);
        data.pop().expect("one result").payload.expect("payload is not none")
    }
}

/// IsCurrentSystemCommitted should hang until when the Paver responds to QueryConfigurationStatus.
#[fasync::run_singlethreaded(test)]
async fn is_current_system_committed_hangs_until_query_configuration_status() {
    let (throttle_hook, throttler) = mphooks::throttle();

    let env = TestEnv::builder()
        .paver_service_builder(MockPaverServiceBuilder::new().insert_hook(throttle_hook))
        .build()
        .await;

    // No paver events yet, so the commit status FIDL server is still hanging.
    // We use timeouts to tell if the FIDL server hangs. This is obviously not ideal, but
    // there does not seem to be a better way of doing it. We considered using `run_until_stalled`,
    // but that's no good because the system-update-committer is running in a seperate process.
    assert_matches!(
        env.commit_status_provider_proxy()
            .is_current_system_committed()
            .map(Some)
            .on_timeout(HANG_DURATION, || None)
            .await,
        None
    );

    // Even after the first paver response, is_current_system_committed should still hang.
    let () = throttler.emit_next_paver_event(&PaverEvent::QueryCurrentConfiguration);
    assert_matches!(
        env.commit_status_provider_proxy()
            .is_current_system_committed()
            .map(Some)
            .on_timeout(HANG_DURATION, || None)
            .await,
        None
    );

    // After the second paver event, we're finally unblocked.
    let () = throttler.emit_next_paver_event(&PaverEvent::QueryConfigurationStatus {
        configuration: Configuration::A,
    });
    env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();
}

/// If the current system is pending commit, the commit status FIDL server should hang
/// until the verifiers start (e.g. after we call QueryConfigurationStatus). Once the
/// verifications complete, we should observe the `USER_0` signal.
#[fasync::run_singlethreaded(test)]
async fn system_pending_commit() {
    let (throttle_hook, throttler) = mphooks::throttle();

    let env = TestEnv::builder()
        .paver_service_builder(
            MockPaverServiceBuilder::new()
                .insert_hook(throttle_hook)
                .insert_hook(mphooks::config_status(|_| Ok(ConfigurationStatus::Pending))),
        )
        .build()
        .await;

    // Emit the first 2 paver events to unblock the FIDL server.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::QueryCurrentConfiguration,
        PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
    ]);
    let event_pair =
        env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();
    assert_eq!(
        event_pair.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
        Err(zx::Status::TIMED_OUT)
    );

    // Once the remaining paver calls are emitted, the system should commit.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::SetConfigurationHealthy { configuration: Configuration::A },
        PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
        PaverEvent::BootManagerFlush,
    ]);
    assert_eq!(OnSignals::new(&event_pair, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));
}

/// If the current system is already committed, the EventPair returned should immediately have
/// `USER_0` asserted.
#[fasync::run_singlethreaded(test)]
async fn system_already_committed() {
    let (throttle_hook, throttler) = mphooks::throttle();

    let env = TestEnv::builder()
        .paver_service_builder(MockPaverServiceBuilder::new().insert_hook(throttle_hook))
        .build()
        .await;

    // Emit the first 2 paver events to unblock the FIDL server.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::QueryCurrentConfiguration,
        PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
    ]);

    // When the commit status FIDL responds, the event pair should immediately observe the signal.
    let event_pair =
        env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();
    assert_eq!(
        event_pair.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
        Ok(zx::Signals::USER_0)
    );
}

/// When the system-update-committer terminates, all clients with handles to the EventPair
/// should observe `EVENTPAIR_CLOSED`.
#[fasync::run_singlethreaded(test)]
async fn eventpair_closed() {
    let env = TestEnv::builder().build().await;
    let event_pair =
        env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();

    drop(env);

    assert_eq!(
        OnSignals::new(&event_pair, zx::Signals::EVENTPAIR_CLOSED).await,
        Ok(zx::Signals::EVENTPAIR_CLOSED | zx::Signals::USER_0)
    );
}

/// There's some complexity with how we handle CommitStatusProvider requests. So, let's do
/// a sanity check to verify our implementation can handle multiple clients.
#[fasync::run_singlethreaded(test)]
async fn multiple_commit_status_provider_requests() {
    let env = TestEnv::builder().build().await;

    let p0 = env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();
    let p1 = env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();

    assert_eq!(
        p0.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
        Ok(zx::Signals::USER_0)
    );
    assert_eq!(
        p1.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
        Ok(zx::Signals::USER_0)
    );
}

/// Make sure the inspect data is plumbed through after successful verifications.
#[fasync::run_singlethreaded(test)]
async fn inspect_health_status_ok() {
    let env = TestEnv::builder()
        // Make sure we run health verifications.
        .paver_service_builder(
            MockPaverServiceBuilder::new()
                .insert_hook(mphooks::config_status(|_| Ok(ConfigurationStatus::Pending))),
        )
        .build()
        .await;

    // Wait for verifications to complete.
    let p = env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();
    assert_eq!(OnSignals::new(&p, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));

    // Observe verification shows up in inspect.
    let hierarchy = env.system_update_committer_inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: {
            "verification": {
                "ota_verification_duration": {
                    "success": AnyProperty,
                }
            },
            "fuchsia.inspect.Health": {
                "start_timestamp_nanos": AnyProperty,
                "status": "OK"
            }
        }
    );
}

/// When the paver fails, the system-update-committer should trigger a reboot regardless of the
/// config. Additionally, the inspect state should reflect the system being unhealthy. We could
/// split this up into several tests, but instead we combine them to reduce redundant lines of code.
/// We could do helper fns, but we decided not to given guidance in
/// https://testing.googleblog.com/2019/12/testing-on-toilet-tests-too-dry-make.html.
#[fasync::run_singlethreaded(test)]
async fn paver_failure_causes_reboot() {
    let (reboot_sender, reboot_recv) = oneshot::channel();
    let reboot_sender = Arc::new(Mutex::new(Some(reboot_sender)));
    let env = TestEnv::builder()
        // Make sure the paver fails.
        .paver_service_builder(MockPaverServiceBuilder::new().insert_hook(mphooks::return_error(
            |e: &PaverEvent| {
                if e == &PaverEvent::QueryCurrentConfiguration {
                    zx::Status::NOT_FOUND
                } else {
                    zx::Status::OK
                }
            },
        )))
        // Make the config say that verification errors should be ignored. This shouldn't have any
        // effect on whether the paver failures cause a reboot.
        .config_data("config.json", json!({"blobfs": "ignore"}).to_string())
        // Handle the reboot requests.
        .reboot_service(MockRebootService::new(Box::new(move |reason: RebootReason| {
            reboot_sender.lock().take().unwrap().send(reason).unwrap();
            Ok(())
        })))
        .build()
        .await;

    assert_eq!(reboot_recv.await, Ok(RebootReason::RetrySystemUpdate));

    let hierarchy = env.system_update_committer_inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: {
            "verification": {},
            "fuchsia.inspect.Health": {
                "message": AnyProperty,
                "start_timestamp_nanos": AnyProperty,
                "status": "UNHEALTHY"
            }
        }
    );
}

/// When the verifications fail and the config says to reboot, we should reboot.
#[fasync::run_singlethreaded(test)]
async fn verification_failure_causes_reboot() {
    let (reboot_sender, reboot_recv) = oneshot::channel();
    let reboot_sender = Arc::new(Mutex::new(Some(reboot_sender)));
    let env = TestEnv::builder()
        // Make sure we run health verifications.
        .paver_service_builder(
            MockPaverServiceBuilder::new()
                .insert_hook(mphooks::config_status(|_| Ok(ConfigurationStatus::Pending))),
        )
        // Make the health verifications fail.
        .verifier_service(MockVerifierService::new(|_| {
            Err(fidl_fuchsia_update_verify::VerifyError::Internal)
        }))
        // Make us reboot on failure.
        .config_data("config.json", json!({"blobfs": "reboot_on_failure"}).to_string())
        // Handle the reboot requests.
        .reboot_service(MockRebootService::new(Box::new(move |reason: RebootReason| {
            reboot_sender.lock().take().unwrap().send(reason).unwrap();
            Ok(())
        })))
        .build()
        .await;

    // We should observe a reboot.
    assert_eq!(reboot_recv.await, Ok(RebootReason::RetrySystemUpdate));

    // Observe failed verification shows up in inspect.
    let hierarchy = env.system_update_committer_inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: {
            "verification": {
                "ota_verification_duration": {
                    "failure_blobfs": AnyProperty,
                },
                "ota_verification_failure": {
                    "blobfs_verify": 1u64,
                }
            },
            "fuchsia.inspect.Health": {
                "message": AnyProperty,
                "start_timestamp_nanos": AnyProperty,
                "status": "UNHEALTHY"
            }
        }
    );
}

/// When the verifications fail and the config says NOT to reboot, we should NOT reboot.
#[fasync::run_singlethreaded(test)]
async fn verification_failure_does_not_cause_reboot() {
    let (reboot_sender, mut reboot_recv) = oneshot::channel();
    let reboot_sender = Arc::new(Mutex::new(Some(reboot_sender)));
    let env = TestEnv::builder()
        // Make sure we run health verifications.
        .paver_service_builder(
            MockPaverServiceBuilder::new()
                .insert_hook(mphooks::config_status(|_| Ok(ConfigurationStatus::Pending))),
        )
        // Make the health verifications fail.
        .verifier_service(MockVerifierService::new(|_| {
            Err(fidl_fuchsia_update_verify::VerifyError::Internal)
        }))
        // Make us IGNORE the verification failure.
        .config_data("config.json", json!({"blobfs": "ignore"}).to_string())
        // Handle the reboot requests.
        .reboot_service(MockRebootService::new(Box::new(move |reason: RebootReason| {
            reboot_sender.lock().take().unwrap().send(reason).unwrap();
            Ok(())
        })))
        .build()
        .await;

    // The commit should happen because the failure was ignored.
    let p = env.commit_status_provider_proxy().is_current_system_committed().await.unwrap();
    assert_eq!(OnSignals::new(&p, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));

    // We should NOT observe a reboot.
    assert_eq!(reboot_recv.try_recv(), Ok(None));

    // Observe failed verification shows up in inspect.
    let hierarchy = env.system_update_committer_inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: {
            "verification": {
                "ota_verification_duration": {
                    "failure_blobfs": AnyProperty,
                },
                "ota_verification_failure": {
                    "blobfs_verify": 1u64,
                }
            },
            "fuchsia.inspect.Health": {
                "start_timestamp_nanos": AnyProperty,
                "status": "OK"
            }
        }
    );
}
