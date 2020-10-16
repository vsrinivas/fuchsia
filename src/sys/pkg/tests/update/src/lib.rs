// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fidl_fuchsia_sys::{LauncherProxy, TerminationReason},
    fidl_fuchsia_update as fidl_update,
    fidl_fuchsia_update_ext::{
        InstallationErrorData, InstallationProgress, InstallingData, State, UpdateInfo,
    },
    fidl_fuchsia_update_installer as fidl_installer,
    fidl_fuchsia_update_installer_ext::{self as installer},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Output},
        server::{NestedEnvironment, ServiceFs},
    },
    futures::prelude::*,
    matches::assert_matches,
    mock_installer::{
        CapturedRebootControllerRequest, CapturedUpdateInstallerRequest, MockUpdateInstallerService,
    },
    parking_lot::Mutex,
    pretty_assertions::assert_eq,
    std::sync::Arc,
};

#[derive(Default)]
struct TestEnvBuilder {
    manager_states: Vec<State>,
    installer_states: Vec<installer::State>,
}

impl TestEnvBuilder {
    fn manager_states(self, manager_states: Vec<State>) -> Self {
        Self { manager_states, ..self }
    }
    fn installer_states(self, installer_states: Vec<installer::State>) -> Self {
        Self { installer_states, ..self }
    }
    fn build(self) -> TestEnv {
        TestEnv::with_states(self.manager_states, self.installer_states)
    }
}

struct TestEnv {
    env: NestedEnvironment,
    update_manager: Arc<MockUpdateManagerService>,
    update_installer: Arc<MockUpdateInstallerService>,
}

impl TestEnv {
    fn builder() -> TestEnvBuilder {
        TestEnvBuilder::default()
    }

    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    fn new() -> Self {
        Self::builder().build()
    }

    fn with_states(states: Vec<State>, installer_states: Vec<installer::State>) -> Self {
        let mut fs = ServiceFs::new();

        let update_manager = Arc::new(MockUpdateManagerService::new(states));
        let update_manager_clone = Arc::clone(&update_manager);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&update_manager_clone).run_service(stream)).detach()
        });

        let update_installer = Arc::new(MockUpdateInstallerService::with_states(installer_states));
        let update_installer_clone = Arc::clone(&update_installer);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&update_installer_clone).run_service(stream)).detach()
        });

        let env = fs
            .create_salted_nested_environment("update_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        Self { env, update_manager, update_installer }
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

    fn assert_update_installer_called_with(
        &self,
        expected_args: Vec<CapturedUpdateInstallerRequest>,
    ) {
        self.update_installer.assert_installer_called_with(expected_args);
    }

    fn assert_reboot_controller_called_with(
        &self,
        expected_requests: Vec<CapturedRebootControllerRequest>,
    ) {
        self.update_installer.assert_reboot_controller_called_with(expected_requests);
    }
}

#[derive(PartialEq, Debug)]
enum CapturedUpdateManagerRequest {
    CheckNow { options: fidl_update::CheckOptions, monitor_present: bool },
}

// fidl_update::CheckOptions does not impl Eq, but it is semantically Eq.
impl Eq for CapturedUpdateManagerRequest {}

struct MockUpdateManagerService {
    states: Vec<State>,
    captured_args: Mutex<Vec<CapturedUpdateManagerRequest>>,
    check_now_response: Mutex<Result<(), fidl_update::CheckNotStartedReason>>,
}

impl MockUpdateManagerService {
    fn new(states: Vec<State>) -> Self {
        Self { states, captured_args: Mutex::new(vec![]), check_now_response: Mutex::new(Ok(())) }
    }
    async fn run_service(self: Arc<Self>, mut stream: fidl_update::ManagerRequestStream) {
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
                        fasync::Task::spawn(Self::send_states(proxy, self.states.clone())).detach();
                    }
                    responder.send(&mut *self.check_now_response.lock()).unwrap();
                }

                fidl_update::ManagerRequest::PerformPendingReboot { responder: _ } => {
                    panic!("update tool should not be calling perform pending reboot!");
                }
            }
        }
    }

    async fn send_states(monitor: fidl_update::MonitorProxy, states: Vec<State>) {
        for state in states.into_iter() {
            monitor.on_state(&mut state.into()).await.unwrap();
        }
    }
}

fn assert_output(output: &Output, expected_stdout: &str, expected_stderr: &str, exit_code: i64) {
    assert_eq!(output.exit_status.reason(), fidl_fuchsia_sys::TerminationReason::Exited);
    let actual_stdout = std::str::from_utf8(&output.stdout).unwrap();
    assert_eq!(actual_stdout, expected_stdout);
    let actual_stderr = std::str::from_utf8(&output.stderr).unwrap();
    assert_eq!(actual_stderr, expected_stderr);
    assert_eq!(output.exit_status.code(), exit_code, "stdout: {}", actual_stdout);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_fails_on_invalid_pkg_url() {
    let env = TestEnv::new();
    let output =
        env.run_update(vec!["force-install", "not-fuchsia-pkg://fuchsia.com/update"]).await;

    assert_matches!(output.exit_status.ok(), Err(_));

    let stderr = std::str::from_utf8(&output.stderr).unwrap();
    assert!(stderr.contains("Error: parsing update package url"), "stderr: {}", stderr);

    env.assert_update_installer_called_with(vec![]);

    env.assert_reboot_controller_called_with(vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_reboot() {
    let update_info = installer::UpdateInfo::builder().download_size(1000).build();
    let env = TestEnv::builder()
        .installer_states(vec![
            installer::State::Prepare,
            installer::State::Fetch(
                installer::UpdateInfoAndProgress::new(update_info, installer::Progress::none())
                    .unwrap(),
            ),
            installer::State::Stage(
                installer::UpdateInfoAndProgress::new(
                    update_info,
                    installer::Progress::builder()
                        .fraction_completed(0.5)
                        .bytes_downloaded(500)
                        .build(),
                )
                .unwrap(),
            ),
            installer::State::WaitToReboot(installer::UpdateInfoAndProgress::done(update_info)),
            installer::State::Reboot(installer::UpdateInfoAndProgress::done(update_info)),
        ])
        .build();

    let output = env.run_update(vec!["force-install", "fuchsia-pkg://fuchsia.com/update"]).await;

    assert_output(
        &output,
        "Installing an update.\n\
        State: Prepare\n\
        State: Fetch(UpdateInfoAndProgress { info: UpdateInfo { download_size: 1000 }, progress: Progress { fraction_completed: 0.0, bytes_downloaded: 0 } })\n\
        State: Stage(UpdateInfoAndProgress { info: UpdateInfo { download_size: 1000 }, progress: Progress { fraction_completed: 0.5, bytes_downloaded: 500 } })\n\
        State: WaitToReboot(UpdateInfoAndProgress { info: UpdateInfo { download_size: 1000 }, progress: Progress { fraction_completed: 1.0, bytes_downloaded: 1000 } })\n",
        "",
        0,
    );

    env.assert_update_installer_called_with(vec![CapturedUpdateInstallerRequest::StartUpdate {
        url: "fuchsia-pkg://fuchsia.com/update".into(),
        options: fidl_installer::Options {
            initiator: Some(fidl_installer::Initiator::User),
            should_write_recovery: Some(true),
            allow_attach_to_existing_attempt: Some(true),
        },
        reboot_controller_present: true,
    }]);

    env.assert_reboot_controller_called_with(vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_no_reboot() {
    let update_info = installer::UpdateInfo::builder().download_size(1000).build();
    let env = TestEnv::builder()
        .installer_states(vec![
            installer::State::Prepare,
            installer::State::Fetch(
                installer::UpdateInfoAndProgress::new(update_info, installer::Progress::none())
                    .unwrap(),
            ),
            installer::State::Stage(
                installer::UpdateInfoAndProgress::new(
                    update_info,
                    installer::Progress::builder()
                        .fraction_completed(0.5)
                        .bytes_downloaded(500)
                        .build(),
                )
                .unwrap(),
            ),
            installer::State::WaitToReboot(installer::UpdateInfoAndProgress::done(update_info)),
            installer::State::DeferReboot(installer::UpdateInfoAndProgress::done(update_info)),
        ])
        .build();
    let output = env
        .run_update(vec!["force-install", "fuchsia-pkg://fuchsia.com/update", "--reboot", "false"])
        .await;

    assert_output(
        &output,
        "Installing an update.\n\
        State: Prepare\n\
        State: Fetch(UpdateInfoAndProgress { info: UpdateInfo { download_size: 1000 }, progress: Progress { fraction_completed: 0.0, bytes_downloaded: 0 } })\n\
        State: Stage(UpdateInfoAndProgress { info: UpdateInfo { download_size: 1000 }, progress: Progress { fraction_completed: 0.5, bytes_downloaded: 500 } })\n\
        State: WaitToReboot(UpdateInfoAndProgress { info: UpdateInfo { download_size: 1000 }, progress: Progress { fraction_completed: 1.0, bytes_downloaded: 1000 } })\n\
        State: DeferReboot(UpdateInfoAndProgress { info: UpdateInfo { download_size: 1000 }, progress: Progress { fraction_completed: 1.0, bytes_downloaded: 1000 } })\n",
        "",
        0,
    );

    env.assert_update_installer_called_with(vec![CapturedUpdateInstallerRequest::StartUpdate {
        url: "fuchsia-pkg://fuchsia.com/update".into(),
        options: fidl_installer::Options {
            initiator: Some(fidl_installer::Initiator::User),
            should_write_recovery: Some(true),
            allow_attach_to_existing_attempt: Some(true),
        },
        reboot_controller_present: true,
    }]);

    env.assert_reboot_controller_called_with(vec![CapturedRebootControllerRequest::Detach]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_failure_state() {
    let env = TestEnv::builder()
        .installer_states(vec![installer::State::Prepare, installer::State::FailPrepare])
        .build();
    let output = env.run_update(vec!["force-install", "fuchsia-pkg://fuchsia.com/update"]).await;

    assert_output(
        &output,
        "Installing an update.\n\
        State: Prepare\n\
        State: FailPrepare\n",
        "Error: Encountered failure state\n",
        1,
    );

    env.assert_update_installer_called_with(vec![CapturedUpdateInstallerRequest::StartUpdate {
        url: "fuchsia-pkg://fuchsia.com/update".into(),
        options: fidl_installer::Options {
            initiator: Some(fidl_installer::Initiator::User),
            should_write_recovery: Some(true),
            allow_attach_to_existing_attempt: Some(true),
        },
        reboot_controller_present: true,
    }]);

    env.assert_reboot_controller_called_with(vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_unexpected_end() {
    let env = TestEnv::builder().installer_states(vec![installer::State::Prepare]).build();
    let output = env.run_update(vec!["force-install", "fuchsia-pkg://fuchsia.com/update"]).await;

    assert_output(
        &output,
        "Installing an update.\n\
        State: Prepare\n",
        "Error: Installation ended unexpectedly\n",
        1,
    );

    env.assert_update_installer_called_with(vec![CapturedUpdateInstallerRequest::StartUpdate {
        url: "fuchsia-pkg://fuchsia.com/update".into(),
        options: fidl_installer::Options {
            initiator: Some(fidl_installer::Initiator::User),
            should_write_recovery: Some(true),
            allow_attach_to_existing_attempt: Some(true),
        },
        reboot_controller_present: true,
    }]);

    env.assert_reboot_controller_called_with(vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_service_initiated_flag() {
    let env = TestEnv::new();
    let _output = env
        .run_update(vec![
            "force-install",
            "fuchsia-pkg://fuchsia.com/update",
            "--service-initiated",
        ])
        .await;

    env.assert_update_installer_called_with(vec![CapturedUpdateInstallerRequest::StartUpdate {
        url: "fuchsia-pkg://fuchsia.com/update".into(),
        options: fidl_installer::Options {
            initiator: Some(fidl_installer::Initiator::Service),
            should_write_recovery: Some(true),
            allow_attach_to_existing_attempt: Some(true),
        },
        reboot_controller_present: true,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_service_initiated_flag() {
    let env = TestEnv::new();
    let output = env.run_update(vec!["check-now", "--service-initiated"]).await;

    assert_output(&output, "Checking for an update.\n", "", 0);
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

    assert_output(&output, "", "Error: Update check failed to start: Throttled\n", 1);
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
    let env = TestEnv::builder()
        .manager_states(vec![
            State::CheckingForUpdates,
            State::InstallingUpdate(InstallingData {
                update: Some(UpdateInfo {
                    version_available: Some("fake-versions".into()),
                    download_size: Some(4),
                }),
                installation_progress: Some(InstallationProgress {
                    fraction_completed: Some(0.5f32),
                }),
            }),
        ])
        .build();
    let output = env.run_update(vec!["check-now", "--monitor"]).await;

    assert_output(
        &output,
        "Checking for an update.\n\
         State: CheckingForUpdates\n\
         State: InstallingUpdate(InstallingData { update: Some(UpdateInfo { version_available: Some(\"fake-versions\"), download_size: Some(4) }), installation_progress: Some(InstallationProgress { fraction_completed: Some(0.5) }) })\n",
         "",
         0,
    );
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
        },
        monitor_present: true,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_monitor_error_checking() {
    let env = TestEnv::builder()
        .manager_states(vec![State::CheckingForUpdates, State::ErrorCheckingForUpdate])
        .build();
    let output = env.run_update(vec!["check-now", "--monitor"]).await;

    assert_output(
        &output,
        "Checking for an update.\n\
         State: CheckingForUpdates\n",
        "Error: Update failed: ErrorCheckingForUpdate\n",
        1,
    );
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
        },
        monitor_present: true,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_monitor_error_installing() {
    let env = TestEnv::builder()
        .manager_states(vec![
            State::CheckingForUpdates,
            State::InstallingUpdate(InstallingData {
                update: Some(UpdateInfo {
                    version_available: Some("fake-versions".into()),
                    download_size: Some(4),
                }),
                installation_progress: Some(InstallationProgress {
                    fraction_completed: Some(0.5f32),
                }),
            }),
            State::InstallationError(InstallationErrorData {
                update: Some(UpdateInfo {
                    version_available: Some("fake-versions".into()),
                    download_size: Some(4),
                }),
                installation_progress: Some(InstallationProgress {
                    fraction_completed: Some(0.5f32),
                }),
            }),
        ])
        .build();
    let output = env.run_update(vec!["check-now", "--monitor"]).await;

    assert_output(
        &output,
        "Checking for an update.\n\
         State: CheckingForUpdates\n\
         State: InstallingUpdate(InstallingData { update: Some(UpdateInfo { version_available: Some(\"fake-versions\"), download_size: Some(4) }), installation_progress: Some(InstallationProgress { fraction_completed: Some(0.5) }) })\n",
        "Error: Update failed: InstallationError(InstallationErrorData { update: Some(UpdateInfo { version_available: Some(\"fake-versions\"), download_size: Some(4) }), installation_progress: Some(InstallationProgress { fraction_completed: Some(0.5) }) })\n",
        1,
    );
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
        },
        monitor_present: true,
    }]);
}
