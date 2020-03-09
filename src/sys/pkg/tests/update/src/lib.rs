// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::Error,
    fidl_fuchsia_sys::{LauncherProxy, TerminationReason},
    fidl_fuchsia_update as fidl_update, fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Output},
        server::{NestedEnvironment, ServiceFs},
    },
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

struct TestEnv {
    env: NestedEnvironment,
    update_manager: Arc<MockUpdateManagerService>,
}

impl TestEnv {
    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    fn new() -> Self {
        let mut fs = ServiceFs::new();

        let update_manager = Arc::new(MockUpdateManagerService::new());
        let update_manager_clone = Arc::clone(&update_manager);
        fs.add_fidl_service(move |stream: fidl_update::ManagerRequestStream| {
            let update_manager_clone = Arc::clone(&update_manager_clone);
            fasync::spawn(
                update_manager_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running update service: {:?}", e)),
            )
        });

        let env = fs
            .create_salted_nested_environment("update_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        Self { env, update_manager }
    }

    async fn run_update<'a>(&'a self, args: Vec<&'a str>) -> Output {
        let launcher = self.launcher();
        let update =
            AppBuilder::new("fuchsia-pkg://fuchsia.com/update-integration-tests#meta/update.cmx")
                .args(args);
        let output = update
            .output(launcher)
            .expect("update to launch")
            .await
            .expect("no errors while waiting for exit");
        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        output
    }

    fn assert_update_manager_called_with(&self, expected_args: Vec<CapturedUpdateManagerRequest>) {
        assert_eq!(*self.update_manager.captured_args.lock(), expected_args);
    }
}

#[derive(PartialEq, Debug)]
enum CapturedUpdateManagerRequest {
    CheckNow { options: fidl_update::CheckOptions, monitor_present: bool },
}

// fidl_update::CheckOptions does not impl Eq, but it is semantically Eq.
impl Eq for CapturedUpdateManagerRequest {}

struct MockUpdateManagerService {
    captured_args: Mutex<Vec<CapturedUpdateManagerRequest>>,
    check_now_response: Mutex<Result<(), fidl_update::CheckNotStartedReason>>,
}

impl MockUpdateManagerService {
    fn new() -> Self {
        Self { captured_args: Mutex::new(vec![]), check_now_response: Mutex::new(Ok(())) }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: fidl_update::ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                fidl_update::ManagerRequest::CheckNow { options, monitor, responder } => {
                    self.captured_args.lock().push(CapturedUpdateManagerRequest::CheckNow {
                        options,
                        monitor_present: monitor.is_some(),
                    });
                    if let Some(monitor) = monitor {
                        let proxy = fidl_update::MonitorProxy::new(
                            fasync::Channel::from_channel(monitor.into_channel()).unwrap(),
                        );
                        fasync::spawn(Self::send_states(proxy));
                    }
                    responder.send(&mut *self.check_now_response.lock()).unwrap();
                }
            }
        }
        Ok(())
    }
    async fn send_states(monitor: fidl_update::MonitorProxy) {
        let states = vec![
            fidl_update::State::CheckingForUpdates(fidl_update::CheckingForUpdatesData {}),
            fidl_update::State::InstallingUpdate(fidl_update::InstallingData {
                update: Some(fidl_update::UpdateInfo {
                    version_available: Some("fake-versions".into()),
                    download_size: Some(4),
                }),
                installation_progress: Some(fidl_update::InstallationProgress {
                    fraction_completed: Some(0.5f32),
                }),
            }),
        ];
        for mut state in states.into_iter() {
            monitor.on_state(&mut state).await.unwrap();
        }
    }
}

fn assert_stdout(output: &Output, expected: &str, exit_code: i64) {
    assert_eq!(output.exit_status.reason(), fidl_fuchsia_sys::TerminationReason::Exited);
    assert_eq!(std::str::from_utf8(&output.stderr).unwrap(), "");
    assert_eq!(
        output.exit_status.code(),
        exit_code,
        "stdout: {}",
        std::str::from_utf8(&output.stdout).unwrap()
    );
    assert_eq!(std::str::from_utf8(output.stdout.as_slice()).unwrap(), expected);
}

fn assert_stderr(output: &Output, expected: &str) {
    assert_eq!(output.exit_status.reason(), fidl_fuchsia_sys::TerminationReason::Exited);
    assert_eq!(std::str::from_utf8(&output.stderr).unwrap(), expected);
    assert_eq!(output.exit_status.code(), 1);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_service_initiated_flag() {
    let env = TestEnv::new();
    let output = env.run_update(vec!["check-now", "--service-initiated"]).await;

    assert_stdout(&output, "Checking for an update.\n", 0);
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::Service),
            allow_attaching_to_existing_update_check: Some(true),
        },
        monitor_present: false,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_error_if_throttled() {
    let env = TestEnv::new();
    *env.update_manager.check_now_response.lock() =
        Err(fidl_update::CheckNotStartedReason::Throttled);
    let output = env.run_update(vec!["check-now"]).await;

    assert_stderr(&output, "Error: Update check failed to start: Throttled\n");
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
        },
        monitor_present: false,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_monitor_flag() {
    let env = TestEnv::new();
    let output = env.run_update(vec!["check-now", "--monitor"]).await;

    assert_stdout(
        &output,
        "Checking for an update.\n\
         State: CheckingForUpdates(CheckingForUpdatesData)\n\
         State: InstallingUpdate(InstallingData { update: Some(UpdateInfo { version_available: Some(\"fake-versions\"), download_size: Some(4) }), installation_progress: Some(InstallationProgress { fraction_completed: Some(0.5) }) })\n", 0);
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
        },
        monitor_present: true,
    }]);
}
