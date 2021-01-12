// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fidl_fuchsia_paver::{Configuration, ConfigurationStatus},
    fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderProxy},
    fuchsia_async::{self as fasync, OnSignals, TimeoutExt},
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::prelude::*,
    matches::assert_matches,
    mock_paver::{hooks as mphooks, MockPaverService, MockPaverServiceBuilder, PaverEvent},
    std::{sync::Arc, time::Duration},
};

const SYSTEM_UPDATE_COMMITTER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/system-update-committer-integration-tests#meta/system-update-committer.cmx";
const HANG_DURATION: Duration = Duration::from_millis(500);

struct TestEnvBuilder {
    paver_service_builder: Option<MockPaverServiceBuilder>,
}
impl TestEnvBuilder {
    fn paver_service_builder(self, paver_service_builder: MockPaverServiceBuilder) -> Self {
        Self { paver_service_builder: Some(paver_service_builder), ..self }
    }

    fn build(self) -> TestEnv {
        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();

        // Set up paver service.
        let paver_service_builder =
            self.paver_service_builder.unwrap_or_else(|| MockPaverServiceBuilder::new());
        let paver_service = Arc::new(paver_service_builder.build());
        let paver_service_clone = Arc::clone(&paver_service);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&paver_service_clone)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach()
        });

        let env = fs
            .create_salted_nested_environment("system_update_committer_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        let system_update_committer = AppBuilder::new(SYSTEM_UPDATE_COMMITTER_CMX.to_owned())
            .spawn(env.launcher())
            .expect("system-update-committer to launch");

        TestEnv { _env: env, system_update_committer, _paver_service: paver_service }
    }
}
struct TestEnv {
    _env: NestedEnvironment,
    system_update_committer: App,
    _paver_service: Arc<MockPaverService>,
}

impl TestEnv {
    fn builder() -> TestEnvBuilder {
        TestEnvBuilder { paver_service_builder: None }
    }

    /// Opens a connection to the fuchsia.update/CommitStatusProvider FIDL service.
    fn commit_status_provider_proxy(&self) -> CommitStatusProviderProxy {
        self.system_update_committer.connect_to_service::<CommitStatusProviderMarker>().unwrap()
    }
}

/// IsCurrentSystemCommitted should hang until when the Paver responds to QueryConfigurationStatus.
#[fasync::run_singlethreaded(test)]
async fn is_current_system_committed_hangs_until_query_configuration_status() {
    let (throttle_hook, throttler) = mphooks::throttle();

    let env = TestEnv::builder()
        .paver_service_builder(MockPaverServiceBuilder::new().insert_hook(throttle_hook))
        .build();

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
        .build();

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
        .build();

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
    let env = TestEnv::builder().build();
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
    let env = TestEnv::builder().build();

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
