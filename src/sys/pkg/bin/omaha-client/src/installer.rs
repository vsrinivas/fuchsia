// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the Fuchsia Installer implementation that talks to fuchsia.update.installer FIDL API.

use crate::install_plan::FuchsiaInstallPlan;
use anyhow::Context;
use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
use fidl_fuchsia_update_installer::{
    InstallerMarker, RebootControllerMarker, RebootControllerProxy,
};
use fuchsia_url::pkg_url::PkgUrl;

use fidl_fuchsia_update_installer_ext::{start_update, Initiator, Options};
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

    /// System update installer error.
    #[error("system update installer failed")]
    Installer,

    /// URL parse error.
    #[error("Parse error")]
    Parse(#[source] fuchsia_url::boot_url::ParseError),
}

#[derive(Debug)]
pub struct FuchsiaInstaller {
    reboot_controller: Option<RebootControllerProxy>,
}

impl FuchsiaInstaller {
    // Unused until temp_installer.rs is removed.
    #[allow(dead_code)]
    pub fn new() -> Result<Self, anyhow::Error> {
        Ok(FuchsiaInstaller { reboot_controller: None })
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
            initiator: match install_plan.install_source {
                InstallSource::ScheduledTask => Initiator::Service,
                InstallSource::OnDemand => Initiator::User,
            },
            should_write_recovery: true,
            allow_attach_to_existing_attempt: true,
        };

        async move {
            let pkgurl = PkgUrl::parse(&url).map_err(FuchsiaInstallError::Parse)?;
            let proxy = fuchsia_component::client::connect_to_service::<InstallerMarker>()
                .map_err(|_| FuchsiaInstallError::Installer)?;

            let (reboot_controller, reboot_controller_server_end) =
                fidl::endpoints::create_proxy::<RebootControllerMarker>()
                    .map_err(FuchsiaInstallError::FIDL)?;

            self.reboot_controller = Some(reboot_controller);

            let mut update_attempt =
                start_update(&pkgurl, options, &proxy, Some(reboot_controller_server_end))
                    .await
                    .map_err(|_| FuchsiaInstallError::Installer)?;

            while let Ok(Some(state)) = update_attempt.try_next().await {
                // TODO: report progress to ProgressObserver
                info!("Installer entered state: {}", state.name());
                if state.is_success() {
                    return Ok(());
                } else if state.is_failure() {
                    return Err(FuchsiaInstallError::Installer);
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
