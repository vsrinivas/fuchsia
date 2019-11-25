// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the Fuchsia Installer implementation that talks to fuchsia.update.installer FIDL API.

#![allow(dead_code)]

use crate::install_plan::FuchsiaInstallPlan;
use failure::Fail;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_update_installer::{
    Initiator, InstallerMarker, InstallerProxy, MonitorEvent, MonitorMarker, MonitorOptions,
    Options, State,
};
use futures::future::BoxFuture;
use futures::prelude::*;
use log::info;
use omaha_client::{
    installer::{Installer, ProgressObserver},
    protocol::request::InstallSource,
};

#[derive(Debug, Fail)]
pub enum FuchsiaInstallError {
    #[fail(display = "generic error: {}", _0)]
    Failure(#[cause] failure::Error),

    #[fail(display = "FIDL error: {}", _0)]
    FIDL(#[cause] fidl::Error),

    #[fail(display = "System update installer failed")]
    Installer,
}

impl From<failure::Error> for FuchsiaInstallError {
    fn from(e: failure::Error) -> FuchsiaInstallError {
        FuchsiaInstallError::Failure(e)
    }
}

impl From<fidl::Error> for FuchsiaInstallError {
    fn from(e: fidl::Error) -> FuchsiaInstallError {
        FuchsiaInstallError::FIDL(e)
    }
}

#[derive(Debug)]
pub struct FuchsiaInstaller {
    proxy: InstallerProxy,
}

impl FuchsiaInstaller {
    pub fn new() -> Result<Self, failure::Error> {
        let proxy = fuchsia_component::client::connect_to_service::<InstallerMarker>()?;
        Ok(FuchsiaInstaller { proxy })
    }

    #[cfg(test)]
    fn new_mock() -> (Self, fidl_fuchsia_update_installer::InstallerRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();
        (FuchsiaInstaller { proxy }, stream)
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
        let url = install_plan.url.to_string();
        let options = Options {
            initiator: Some(match install_plan.install_source {
                InstallSource::ScheduledTask => Initiator::Service,
                InstallSource::OnDemand => Initiator::User,
            }),
        };

        async move {
            let (monitor_proxy, server_end) = create_proxy::<MonitorMarker>()?;
            let monitor_options = MonitorOptions { should_notify: Some(true) };
            let attempt_id =
                self.proxy.start_update(&url, options, server_end, monitor_options).await?;
            info!("Update started with attempt id: {}", attempt_id);

            let mut stream = monitor_proxy.take_event_stream();
            while let Some(event) = stream.try_next().await? {
                match event {
                    MonitorEvent::OnStateEnter { state } => {
                        // TODO: report progress to ProgressObserver
                        info!("Installer entered state: {}", state_to_string(state));
                        match state {
                            State::Complete => {
                                return Ok(());
                            }
                            State::Fail => {
                                return Err(FuchsiaInstallError::Installer);
                            }
                            _ => {}
                        }
                    }
                }
            }

            Err(FuchsiaInstallError::Installer)
        }
            .boxed()
    }
}

/// Convert fuchsia.update.installer/State to string for ProgressObserver.
fn state_to_string(state: State) -> &'static str {
    match state {
        State::Prepare => "Prepare",
        State::Download => "Download",
        State::Stage => "Stage",
        State::Reboot => "Reboot",
        State::Finalize => "Finalize",
        State::Complete => "Complete",
        State::Fail => "Fail",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_update_installer::InstallerRequest;
    use fuchsia_async as fasync;

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

    #[fasync::run_singlethreaded(test)]
    async fn test_start_update() {
        let (mut installer, mut stream) = FuchsiaInstaller::new_mock();
        let plan = FuchsiaInstallPlan {
            url: TEST_URL.parse().unwrap(),
            install_source: InstallSource::OnDemand,
        };
        let installer_fut = async move {
            installer.perform_install(&plan, None).await.unwrap();
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate {
                    url,
                    options: Options { initiator },
                    monitor,
                    monitor_options: MonitorOptions { should_notify },
                    responder,
                }) => {
                    assert_eq!(url, TEST_URL);
                    assert_eq!(initiator, Some(Initiator::User));
                    assert_eq!(should_notify, Some(true));
                    responder.send("00000000-0000-0000-0000-000000000001").unwrap();
                    let (_stream, handle) = monitor.into_stream_and_control_handle().unwrap();
                    handle.send_on_state_enter(State::Complete).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_install_fail() {
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
                    responder.send("00000000-0000-0000-0000-000000000002").unwrap();
                    let (_stream, handle) = monitor.into_stream_and_control_handle().unwrap();
                    handle.send_on_state_enter(State::Fail).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_fail() {
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
                    responder.send("00000000-0000-0000-0000-000000000003").unwrap();
                    let (_stream, handle) = monitor.into_stream_and_control_handle().unwrap();
                    handle.send_on_state_enter(State::Prepare).unwrap();
                    handle.send_on_state_enter(State::Download).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }
}
