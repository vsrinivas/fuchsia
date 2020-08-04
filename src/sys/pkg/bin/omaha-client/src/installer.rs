// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the Fuchsia Installer implementation that talks to fuchsia.update.installer FIDL API.

use crate::install_plan::FuchsiaInstallPlan;
use anyhow::Context;
use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
use fidl_fuchsia_pkg::PackageUrl;
use fidl_fuchsia_update_installer::{
    Initiator, InstallerMarker, InstallerProxy, MonitorMarker, MonitorRequest, Options,
    RebootControllerMarker, RebootControllerProxy, State, UpdateNotStartedReason,
};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::future::BoxFuture;
use futures::prelude::*;
use log::info;
use omaha_client::{
    installer::{Installer, ProgressObserver},
    protocol::request::InstallSource,
};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum FuchsiaInstallError {
    #[error("generic error: {0}")]
    Failure(#[from] anyhow::Error),

    #[error("FIDL error: {0}")]
    FIDL(#[from] fidl::Error),

    #[error("an installation was already in progress")]
    InstallInProgress,

    #[error("system update installer failed")]
    Installer,
}

#[derive(Debug)]
pub struct FuchsiaInstaller {
    proxy: InstallerProxy,
    reboot_controller: Option<RebootControllerProxy>,
}

impl FuchsiaInstaller {
    // Unused until temp_installer.rs is removed.
    #[allow(dead_code)]
    pub fn new() -> Result<Self, anyhow::Error> {
        let proxy = fuchsia_component::client::connect_to_service::<InstallerMarker>()?;
        Ok(FuchsiaInstaller { proxy, reboot_controller: None })
    }

    #[cfg(test)]
    fn new_mock() -> (Self, fidl_fuchsia_update_installer::InstallerRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();
        (FuchsiaInstaller { proxy, reboot_controller: None }, stream)
    }
}

impl Installer for FuchsiaInstaller {
    type InstallPlan = FuchsiaInstallPlan;
    type Error = FuchsiaInstallError;

    fn perform_install(
        &mut self,
        install_plan: &FuchsiaInstallPlan,
        _observer: Option<&dyn ProgressObserver>,
    ) -> BoxFuture<'_, Result<(), FuchsiaInstallError>> {
        let mut url = PackageUrl { url: install_plan.url.to_string() };
        let options = Options {
            initiator: Some(match install_plan.install_source {
                InstallSource::ScheduledTask => Initiator::Service,
                InstallSource::OnDemand => Initiator::User,
            }),
            should_write_recovery: None,
            allow_attach_to_existing_attempt: None,
        };

        async move {
            let (monitor_client_end, mut monitor) =
                fidl::endpoints::create_request_stream::<MonitorMarker>()?;
            let (reboot_controller, reboot_controller_server_end) =
                fidl::endpoints::create_proxy::<RebootControllerMarker>()?;
            self.reboot_controller = Some(reboot_controller);
            let attempt_id = self
                .proxy
                .start_update(
                    &mut url,
                    options,
                    monitor_client_end,
                    Some(reboot_controller_server_end),
                )
                .await?
                .map_err(|reason| match reason {
                    UpdateNotStartedReason::AlreadyInProgress => {
                        FuchsiaInstallError::InstallInProgress
                    }
                })?;
            info!("Update started with attempt id: {}", attempt_id);

            while let Some(request) = monitor.try_next().await? {
                let MonitorRequest::OnState { state, responder } = request;
                let _ = responder.send();

                // TODO: report progress to ProgressObserver
                info!("Installer entered state: {}", state_to_string(&state));
                match state {
                    State::Complete(_) | State::Reboot(_) | State::DeferReboot(_) => {
                        return Ok(());
                    }
                    State::FailPrepare(_) | State::FailFetch(_) | State::FailStage(_) => {
                        return Err(FuchsiaInstallError::Installer);
                    }
                    _ => {}
                }
            }

            Err(FuchsiaInstallError::Installer)
        }
        .boxed()
    }

    fn perform_reboot(&mut self) -> BoxFuture<'_, Result<(), anyhow::Error>> {
        async move {
            match self.reboot_controller.take() {
                Some(reboot_controller) => {
                    reboot_controller.unblock().context("notify installer it can reboot when ready")
                }
                None => {
                    // FIXME Need the direct reboot path anymore?
                    connect_to_service::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()?
                        .reboot(RebootReason::SystemUpdate)
                        .await?
                        .map_err(zx::Status::from_raw)
                        .context("reboot error")
                }
            }
        }
        .boxed()
    }
}

/// Convert fuchsia.update.installer/State to string for ProgressObserver.
fn state_to_string(state: &State) -> &'static str {
    match state {
        State::Prepare(_) => "Prepare",
        State::Fetch(_) => "Fetch",
        State::Stage(_) => "Stage",
        State::Reboot(_) => "Reboot",
        State::DeferReboot(_) => "DeferReboot",
        State::WaitToReboot(_) => "WaitToReboot",
        State::Complete(_) => "Complete",
        State::FailPrepare(_) => "FailPrepare",
        State::FailFetch(_) => "FailFetch",
        State::FailStage(_) => "FailStage",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_update_installer::{InstallationProgress, InstallerRequest, UpdateInfo};
    use fuchsia_async as fasync;
    use matches::assert_matches;

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

    #[fasync::run_singlethreaded(test)]
    async fn test_start_update() {
        let (mut installer, mut stream) = FuchsiaInstaller::new_mock();
        let plan = FuchsiaInstallPlan {
            url: TEST_URL.parse().unwrap(),
            install_source: InstallSource::OnDemand,
        };
        let installer_fut = async move {
            let () = installer.perform_install(&plan, None).await.unwrap();
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate {
                    url,
                    options:
                        Options { initiator, should_write_recovery, allow_attach_to_existing_attempt },
                    monitor,
                    reboot_controller,
                    responder,
                }) => {
                    assert_eq!(url.url, TEST_URL);
                    assert_eq!(initiator, Some(Initiator::User));
                    assert_matches!(reboot_controller, Some(_));
                    assert_eq!(should_write_recovery, None);
                    assert_eq!(allow_attach_to_existing_attempt, None);
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000001".to_owned()))
                        .unwrap();
                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut State::Reboot(fidl_fuchsia_update_installer::RebootData {
                            info: Some(UpdateInfo { download_size: None }),
                            progress: Some(InstallationProgress {
                                fraction_completed: Some(1.0),
                                bytes_downloaded: None,
                            }),
                        }))
                        .await
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_install_error() {
        let (mut installer, mut stream) = FuchsiaInstaller::new_mock();
        let plan = FuchsiaInstallPlan {
            url: TEST_URL.parse().unwrap(),
            install_source: InstallSource::OnDemand,
        };
        let installer_fut = async move {
            match installer.perform_install(&plan, None).await {
                Err(FuchsiaInstallError::Installer) => {} // expected
                result => panic!("Unexpected result: {:?}", result),
            }
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000002".to_owned()))
                        .unwrap();

                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut State::FailPrepare(
                            fidl_fuchsia_update_installer::FailPrepareData {},
                        ))
                        .await
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_error() {
        let (mut installer, mut stream) = FuchsiaInstaller::new_mock();
        let plan = FuchsiaInstallPlan {
            url: TEST_URL.parse().unwrap(),
            install_source: InstallSource::OnDemand,
        };
        let installer_fut = async move {
            match installer.perform_install(&plan, None).await {
                Err(FuchsiaInstallError::FIDL(_)) => {} // expected
                result => panic!("Unexpected result: {:?}", result),
            }
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { .. }) => {
                    // Don't send attempt id.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_server_close_unexpectedly() {
        let (mut installer, mut stream) = FuchsiaInstaller::new_mock();
        let plan = FuchsiaInstallPlan {
            url: TEST_URL.parse().unwrap(),
            install_source: InstallSource::OnDemand,
        };
        let installer_fut = async move {
            match installer.perform_install(&plan, None).await {
                Err(FuchsiaInstallError::Installer) => {} // expected
                result => panic!("Unexpected result: {:?}", result),
            }
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000003".to_owned()))
                        .unwrap();

                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut State::Prepare(
                            fidl_fuchsia_update_installer::PrepareData {},
                        ))
                        .await
                        .unwrap();
                    let () = monitor
                        .on_state(&mut State::Fetch(fidl_fuchsia_update_installer::FetchData {
                            info: Some(UpdateInfo { download_size: None }),
                            progress: Some(InstallationProgress {
                                fraction_completed: Some(0.0),
                                bytes_downloaded: None,
                            }),
                        }))
                        .await
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }
}
