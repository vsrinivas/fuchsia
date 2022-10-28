// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![cfg(test)]
use {
    assert_matches::assert_matches,
    fidl_fuchsia_paver::Configuration,
    fidl_fuchsia_update as fidl_update,
    fidl_fuchsia_update_ext::{
        InstallationErrorData, InstallationProgress, InstallingData, State, UpdateInfo,
    },
    fidl_fuchsia_update_installer as fidl_installer,
    fidl_fuchsia_update_installer_ext::{self as installer},
    fuchsia_async::{self as fasync, TimeoutExt as _},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx, EventPair, HandleBased, Peered},
    futures::{channel::mpsc, lock::Mutex as AsyncMutex, prelude::*},
    mock_installer::{
        CapturedRebootControllerRequest, CapturedUpdateInstallerRequest, MockUpdateInstallerService,
    },
    mock_paver::{MockPaverService, MockPaverServiceBuilder, PaverEvent},
    mock_reboot::{MockRebootService, RebootReason},
    parking_lot::Mutex,
    pretty_assertions::assert_eq,
    std::{sync::Arc, time::Duration},
};

const BINARY_PATH: &str = "/pkg/bin/update";

async fn run_commit_status_provider_service(
    mut stream: fidl_update::CommitStatusProviderRequestStream,
    p: Arc<EventPair>,
) {
    while let Some(req) = stream.try_next().await.unwrap() {
        let fidl_update::CommitStatusProviderRequest::IsCurrentSystemCommitted { responder } = req;
        let pair = p.duplicate_handle(zx::Rights::BASIC).unwrap();
        let () = responder.send(pair).unwrap();
    }
}

#[derive(Default)]
struct TestEnvBuilder {
    manager_states: Vec<State>,
    manager_states_receiver: Option<mpsc::Receiver<Vec<State>>>,
    installer_states: Vec<installer::State>,
    commit_status_provider_response: Option<EventPair>,
    paver_service: Option<MockPaverService>,
    reboot_service: Option<MockRebootService>,
}

impl TestEnvBuilder {
    fn manager_states(self, manager_states: Vec<State>) -> Self {
        Self { manager_states, ..self }
    }

    fn manager_states_receiver(self, manager_states_receiver: mpsc::Receiver<Vec<State>>) -> Self {
        Self { manager_states_receiver: Some(manager_states_receiver), ..self }
    }

    fn installer_states(self, installer_states: Vec<installer::State>) -> Self {
        Self { installer_states, ..self }
    }

    fn commit_status_provider_response(self, response: EventPair) -> Self {
        Self { commit_status_provider_response: Some(response), ..self }
    }

    fn paver_service(self, paver_service: MockPaverService) -> Self {
        Self { paver_service: Some(paver_service), ..self }
    }

    fn reboot_service(self, reboot_service: MockRebootService) -> Self {
        Self { reboot_service: Some(reboot_service), ..self }
    }

    fn build(self) -> TestEnv {
        let mut fs = ServiceFs::new();

        let manager_states_receiver = match self.manager_states_receiver {
            Some(states_receiver) => states_receiver,
            None => {
                let (mut sender, receiver) = mpsc::channel(0);
                let manager_states = self.manager_states;
                fasync::Task::spawn(async move {
                    sender.send(manager_states).await.unwrap();
                })
                .detach();
                receiver
            }
        };
        let update_manager = Arc::new(MockUpdateManagerService::new(manager_states_receiver));
        let update_manager_clone = Arc::clone(&update_manager);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&update_manager_clone).run_service(stream)).detach()
        });

        let update_installer =
            Arc::new(MockUpdateInstallerService::with_states(self.installer_states));
        let update_installer_clone = Arc::clone(&update_installer);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(Arc::clone(&update_installer_clone).run_service(stream)).detach()
        });

        if let Some(response) = self.commit_status_provider_response {
            let response = Arc::new(response);
            fs.add_fidl_service(move |stream| {
                fasync::Task::spawn(run_commit_status_provider_service(
                    stream,
                    Arc::clone(&response),
                ))
                .detach()
            });
        }

        if let Some(paver_service) = self.paver_service {
            let paver_service = Arc::new(paver_service);
            fs.add_fidl_service(move |stream| {
                fasync::Task::spawn(
                    Arc::clone(&paver_service)
                        .run_paver_service(stream)
                        .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                )
                .detach()
            });
        }

        if let Some(reboot_service) = self.reboot_service {
            let reboot_service = Arc::new(reboot_service);
            fs.add_fidl_service(move |stream| {
                fasync::Task::spawn(
                    Arc::clone(&reboot_service)
                        .run_reboot_service(stream)
                        .unwrap_or_else(|e| panic!("error running reboot service: {:?}", e)),
                )
                .detach()
            });
        }

        let (svc_proxy, svc_server_end) = fidl::endpoints::create_proxy().expect("create channel");

        let _env = fs.serve_connection(svc_server_end).expect("serve connection");

        fasync::Task::spawn(fs.collect()).detach();

        TestEnv { update_manager, update_installer, svc_proxy }
    }
}

struct TestEnv {
    update_manager: Arc<MockUpdateManagerService>,
    update_installer: Arc<MockUpdateInstallerService>,
    svc_proxy: fidl_fuchsia_io::DirectoryProxy,
}

impl TestEnv {
    fn builder() -> TestEnvBuilder {
        TestEnvBuilder::default()
    }

    fn new() -> Self {
        Self::builder().build()
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
    states_receiver: AsyncMutex<mpsc::Receiver<Vec<State>>>,
    captured_args: Mutex<Vec<CapturedUpdateManagerRequest>>,
    check_now_response: Mutex<Result<(), fidl_update::CheckNotStartedReason>>,
}

impl MockUpdateManagerService {
    fn new(states_receiver: mpsc::Receiver<Vec<State>>) -> Self {
        Self {
            states_receiver: AsyncMutex::new(states_receiver),
            captured_args: Mutex::new(vec![]),
            check_now_response: Mutex::new(Ok(())),
        }
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
                        let mut receiver = self.states_receiver.lock().await;
                        let states = receiver.next().await.unwrap();
                        fasync::Task::spawn(Self::send_states(proxy, states)).detach();
                    }
                    responder.send(&mut self.check_now_response.lock()).unwrap();
                }

                fidl_update::ManagerRequest::PerformPendingReboot { responder: _ } => {
                    panic!("update tool should not be calling perform pending reboot!");
                }

                fidl_update::ManagerRequest::MonitorAllUpdateChecks {
                    attempts_monitor,
                    control_handle: _,
                } => {
                    let proxy = attempts_monitor.into_proxy().unwrap();
                    let mut receiver = self.states_receiver.lock().await;
                    while let Some(states) = receiver.next().await {
                        let (monitor, server_end) =
                            fidl::endpoints::create_proxy::<fidl_update::MonitorMarker>().unwrap();
                        let mut options = fidl_update::AttemptOptions::EMPTY;
                        options.initiator = Some(fidl_update::Initiator::Service);
                        proxy.on_start(options, server_end).await.unwrap();
                        Self::send_states(monitor, states).await;
                    }
                }
            }
        }
    }

    async fn send_states(monitor: fidl_update::MonitorProxy, states: Vec<State>) {
        for state in states {
            monitor.on_state(&mut state.into()).await.unwrap();
        }
    }
}

fn assert_output(
    output: &shell_process::ProcessOutput,
    expected_stdout: &str,
    expected_stderr: &str,
    exit_code: i64,
) {
    let actual_stderr = output.stderr_str();
    let actual_stdout = output.stdout_str();
    assert_eq!(output.return_code(), exit_code);
    assert_eq!(actual_stderr, expected_stderr);
    assert_eq!(actual_stdout, expected_stdout);
}

async fn assert_async_output(socket: &mut fasync::Socket, expected_output: &str) {
    let mut actual_output = vec![0; expected_output.len()];
    socket
        .read_exact(&mut actual_output[..])
        .on_timeout(Duration::from_secs(1), || panic!("reading output timed out"))
        .await
        .unwrap();
    let actual_output = std::str::from_utf8(&actual_output).unwrap();
    assert_eq!(actual_output, expected_output);
}

fn assert_async_output_not_ready(socket: &mut fasync::Socket) {
    assert_matches!(socket.read(&mut [0; 1]).now_or_never(), None);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_fails_on_invalid_pkg_url() {
    let env = TestEnv::new();
    let output = shell_process::run_process(
        BINARY_PATH,
        ["force-install", "not-fuchsia-pkg://fuchsia.com/update"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

    assert!(!output.is_ok());

    let stderr = output.stderr_str();
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

    let output = shell_process::run_process(
        BINARY_PATH,
        ["force-install", "fuchsia-pkg://fuchsia.com/update"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

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
            ..fidl_installer::Options::EMPTY
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
    let output = shell_process::run_process(
        BINARY_PATH,
        ["force-install", "fuchsia-pkg://fuchsia.com/update", "--reboot", "false"],
        [("/svc", &env.svc_proxy)],
    )
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
            ..fidl_installer::Options::EMPTY
        },
        reboot_controller_present: true,
    }]);

    env.assert_reboot_controller_called_with(vec![CapturedRebootControllerRequest::Detach]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_failure_state() {
    let env = TestEnv::builder()
        .installer_states(vec![
            installer::State::Prepare,
            installer::State::FailPrepare(installer::PrepareFailureReason::Internal),
        ])
        .build();
    let output = shell_process::run_process(
        BINARY_PATH,
        ["force-install", "fuchsia-pkg://fuchsia.com/update"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

    assert_output(
        &output,
        "Installing an update.\n\
        State: Prepare\n\
        State: FailPrepare(Internal)\n",
        "Error: Encountered failure state\n",
        1,
    );

    env.assert_update_installer_called_with(vec![CapturedUpdateInstallerRequest::StartUpdate {
        url: "fuchsia-pkg://fuchsia.com/update".into(),
        options: fidl_installer::Options {
            initiator: Some(fidl_installer::Initiator::User),
            should_write_recovery: Some(true),
            allow_attach_to_existing_attempt: Some(true),
            ..fidl_installer::Options::EMPTY
        },
        reboot_controller_present: true,
    }]);

    env.assert_reboot_controller_called_with(vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_unexpected_end() {
    let env = TestEnv::builder().installer_states(vec![installer::State::Prepare]).build();
    let output = shell_process::run_process(
        BINARY_PATH,
        ["force-install", "fuchsia-pkg://fuchsia.com/update"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

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
            ..fidl_installer::Options::EMPTY
        },
        reboot_controller_present: true,
    }]);

    env.assert_reboot_controller_called_with(vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn force_install_service_initiated_flag() {
    let env = TestEnv::new();
    let _output = shell_process::run_process(
        BINARY_PATH,
        ["force-install", "fuchsia-pkg://fuchsia.com/update", "--service-initiated"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

    env.assert_update_installer_called_with(vec![CapturedUpdateInstallerRequest::StartUpdate {
        url: "fuchsia-pkg://fuchsia.com/update".into(),
        options: fidl_installer::Options {
            initiator: Some(fidl_installer::Initiator::Service),
            should_write_recovery: Some(true),
            allow_attach_to_existing_attempt: Some(true),
            ..fidl_installer::Options::EMPTY
        },
        reboot_controller_present: true,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_service_initiated_flag() {
    let env = TestEnv::new();
    let output = shell_process::run_process(
        BINARY_PATH,
        ["check-now", "--service-initiated"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

    assert_output(&output, "Checking for an update.\n", "", 0);
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::Service),
            allow_attaching_to_existing_update_check: Some(true),
            ..fidl_update::CheckOptions::EMPTY
        },
        monitor_present: false,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_error_if_throttled() {
    let env = TestEnv::new();
    *env.update_manager.check_now_response.lock() =
        Err(fidl_update::CheckNotStartedReason::Throttled);
    let output =
        shell_process::run_process(BINARY_PATH, ["check-now"], [("/svc", &env.svc_proxy)]).await;

    assert_output(&output, "", "Error: Update check failed to start: Throttled\n", 1);
    env.assert_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::CheckOptions {
            initiator: Some(fidl_update::Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
            ..fidl_update::CheckOptions::EMPTY
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
    let output = shell_process::run_process(
        BINARY_PATH,
        ["check-now", "--monitor"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

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
            ..fidl_update::CheckOptions::EMPTY
        },
        monitor_present: true,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn check_now_monitor_error_checking() {
    let env = TestEnv::builder()
        .manager_states(vec![State::CheckingForUpdates, State::ErrorCheckingForUpdate])
        .build();
    let output = shell_process::run_process(
        BINARY_PATH,
        ["check-now", "--monitor"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

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
            ..fidl_update::CheckOptions::EMPTY
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
    let output = shell_process::run_process(
        BINARY_PATH,
        ["check-now", "--monitor"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

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
            ..fidl_update::CheckOptions::EMPTY
        },
        monitor_present: true,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn monitor_all_update_checks() {
    let (mut sender, receiver) = mpsc::channel(0);
    let env = TestEnv::builder().manager_states_receiver(receiver).build();
    let (update, mut stdout, mut stderr) = shell_process::run_process_async(
        BINARY_PATH,
        ["monitor-updates"],
        [("/svc", &env.svc_proxy)],
    )
    .await;

    assert_async_output_not_ready(&mut stdout);
    assert_async_output_not_ready(&mut stderr);

    sender.send(vec![State::CheckingForUpdates, State::NoUpdateAvailable]).await.unwrap();
    assert_async_output(
        &mut stdout,
        "Service started an update attempt\n\
         State: CheckingForUpdates\n\
         State: NoUpdateAvailable\n",
    )
    .await;

    assert_async_output_not_ready(&mut stdout);
    assert_async_output_not_ready(&mut stderr);

    sender
        .send(vec![
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
        .await
        .unwrap();
    assert_async_output(
        &mut stdout,
        "Service started an update attempt\n\
         State: CheckingForUpdates\n\
         State: InstallingUpdate(InstallingData { update: Some(UpdateInfo { version_available: Some(\"fake-versions\"), download_size: Some(4) }), installation_progress: Some(InstallationProgress { fraction_completed: Some(0.5) }) })\n",
    )
    .await;
    assert_async_output(
        &mut stderr,
        "Error: Update failed: InstallationError(InstallationErrorData { update: Some(UpdateInfo { version_available: Some(\"fake-versions\"), download_size: Some(4) }), installation_progress: Some(InstallationProgress { fraction_completed: Some(0.5) }) })\n",
      )
    .await;

    assert_async_output_not_ready(&mut stdout);
    assert_async_output_not_ready(&mut stderr);

    drop(sender);
    assert_eq!(update.await, 0); // exit status OK.
}

#[fasync::run_singlethreaded(test)]
async fn wait_for_commit_success() {
    let (p0, p1) = EventPair::create().unwrap();
    let env = TestEnv::builder().commit_status_provider_response(p1).build();

    let () = p0.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).unwrap();

    let output =
        shell_process::run_process(BINARY_PATH, ["wait-for-commit"], [("/svc", &env.svc_proxy)])
            .await;

    assert_output(
        &output,
        "Waiting for commit.\n\
         Committed!\n",
        "",
        0,
    );
}

#[fasync::run_singlethreaded(test)]
async fn revert_success() {
    #[derive(Debug, PartialEq)]
    enum Interaction {
        Paver(PaverEvent),
        Reboot(RebootReason),
    }
    let interactions = Arc::new(Mutex::new(vec![]));
    let env = TestEnv::builder()
        .paver_service({
            let interactions = Arc::clone(&interactions);
            MockPaverServiceBuilder::new()
                .event_hook(move |event| {
                    interactions.lock().push(Interaction::Paver(event.clone()));
                })
                .build()
        })
        .reboot_service({
            let interactions = Arc::clone(&interactions);
            MockRebootService::new(Box::new(move |reason| {
                interactions.lock().push(Interaction::Reboot(reason));
                Ok(())
            }))
        })
        .build();

    let output =
        shell_process::run_process(BINARY_PATH, ["revert"], [("/svc", &env.svc_proxy)]).await;

    assert_output(&output, "Reverting the update.\n", "", 0);
    assert_eq!(
        interactions.lock().as_slice(),
        &[
            Interaction::Paver(PaverEvent::QueryCurrentConfiguration),
            Interaction::Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: Configuration::A
            }),
            Interaction::Paver(PaverEvent::BootManagerFlush),
            Interaction::Reboot(RebootReason::UserRequest)
        ]
    );
}
