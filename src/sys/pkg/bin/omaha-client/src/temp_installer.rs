// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a temporary Installer implementation that launches system updater directly, we will
//! delete this and switch to the one in mod installer once the FIDL API is ready.

use crate::install_plan::FuchsiaInstallPlan;
use failure::Fail;
use fidl_fuchsia_sys::LauncherProxy;
use fuchsia_component::client::{launcher, AppBuilder, ExitStatus};
use futures::future::BoxFuture;
use futures::prelude::*;
use omaha_client::{
    installer::{Installer, ProgressObserver},
    protocol::request::InstallSource,
};

type Result<T> = std::result::Result<T, FuchsiaInstallError>;

#[derive(Debug, Fail)]
pub enum FuchsiaInstallError {
    #[fail(display = "generic error: {}", _0)]
    Failure(#[cause] failure::Error),

    #[fail(display = "System update installer failed: {}", _0)]
    Installer(#[cause] ExitStatus),
}

impl From<failure::Error> for FuchsiaInstallError {
    fn from(e: failure::Error) -> FuchsiaInstallError {
        FuchsiaInstallError::Failure(e)
    }
}

#[derive(Debug)]
pub struct FuchsiaInstaller {
    launcher: LauncherProxy,
}

impl FuchsiaInstaller {
    pub fn new() -> Result<Self> {
        Ok(FuchsiaInstaller { launcher: launcher()? })
    }

    #[cfg(test)]
    fn new_mock() -> (Self, fidl_fuchsia_sys::LauncherRequestStream) {
        let (launcher, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_sys::LauncherMarker>().unwrap();
        (FuchsiaInstaller { launcher }, stream)
    }
}

impl Installer for FuchsiaInstaller {
    type InstallPlan = FuchsiaInstallPlan;
    type Error = FuchsiaInstallError;

    fn perform_install(
        &mut self,
        install_plan: &FuchsiaInstallPlan,
        _observer: Option<&dyn ProgressObserver>,
    ) -> BoxFuture<'_, Result<()>> {
        let url = install_plan.url.to_string();
        let initiator = match install_plan.install_source {
            InstallSource::ScheduledTask => "automatic",
            InstallSource::OnDemand => "manual",
        };
        async move {
            AppBuilder::new("fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx")
                .arg("-initiator")
                .arg(initiator)
                .arg("-update")
                .arg(url)
                .arg("-reboot=false")
                .status(&self.launcher)?
                .await?
                .ok()
                .map_err(FuchsiaInstallError::Installer)
        }
        .boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_sys::{LaunchInfo, LauncherRequest, TerminationReason};
    use fuchsia_async as fasync;

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

    #[fasync::run_singlethreaded(test)]
    async fn test_perform_install() {
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
                Ok(LauncherRequest::CreateComponent {
                    launch_info: LaunchInfo { url, arguments, .. },
                    controller: Some(controller),
                    ..
                }) => {
                    assert_eq!(url, "fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx");
                    assert_eq!(
                        arguments,
                        Some(vec![
                            "-initiator".to_string(),
                            "manual".to_string(),
                            "-update".to_string(),
                            TEST_URL.to_string(),
                            "-reboot=false".to_string()
                        ])
                    );
                    let (_stream, handle) = controller.into_stream_and_control_handle().unwrap();
                    handle.send_on_terminated(0, TerminationReason::Exited).unwrap();
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
                Err(FuchsiaInstallError::Installer(_)) => {} // expected
                result => panic!("Unexpected result: {:?}", result),
            }
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(LauncherRequest::CreateComponent { controller: Some(controller), .. }) => {
                    let (_stream, handle) = controller.into_stream_and_control_handle().unwrap();
                    handle.send_on_terminated(1, TerminationReason::Exited).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }
}
