// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy, UpdatePolicy},
    fidl_fuchsia_update_installer::{
        Initiator, InstallerMarker, InstallerProxy, MonitorMarker, MonitorRequest, Options,
        RebootControllerMarker, State,
    },
    fidl_fuchsia_update_usb::{
        CheckError, CheckSuccess, CheckerRequest, CheckerRequestStream,
        MonitorMarker as UsbMonitorMarker,
    },
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon::Status,
    futures::prelude::*,
    std::{cmp::Ordering, str::FromStr},
    update_package::{SystemVersion, UpdatePackage},
};

const BUILD_INFO_VERSION_PATH: &str = "/config/build-info/version";

pub struct UsbUpdateChecker<'a> {
    build_info_path: &'a str,
}

impl UsbUpdateChecker<'_> {
    pub fn new() -> Self {
        UsbUpdateChecker { build_info_path: BUILD_INFO_VERSION_PATH }
    }

    pub async fn handle_request_stream(
        &self,
        stream: &mut CheckerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Getting request from stream")? {
            match request {
                CheckerRequest::Check { update_url, logs_dir, monitor, responder } => {
                    let mut result = self.do_check(&update_url.url, logs_dir, monitor).await;
                    responder.send(&mut result).context("Sending check result")?;
                }
            }
        }

        Ok(())
    }

    async fn do_check(
        &self,
        unpinned_update_url: &str,
        _logs_dir: Option<fidl::endpoints::ClientEnd<DirectoryMarker>>,
        monitor: Option<fidl::endpoints::ClientEnd<UsbMonitorMarker>>,
    ) -> Result<CheckSuccess, CheckError> {
        let unpinned_update_url = PkgUrl::parse(unpinned_update_url)
            .map_err(|e| {
                fx_log_warn!(
                    "Failed to parse update_url '{}': {:#}",
                    unpinned_update_url,
                    anyhow!(e)
                );
                CheckError::InvalidUpdateUrl
            })
            .and_then(|url| self.validate_url(url))?;

        let (package, pinned_update_url) =
            self.open_update_package(unpinned_update_url).await.map_err(|e| {
                fx_log_warn!("Failed to open update package: {:#}", anyhow!(e));
                CheckError::UpdateFailed
            })?;

        let package_version = package.version().await.map_err(|e| {
            fx_log_warn!("Failed to read update package version: {:#}", anyhow!(e));
            CheckError::UpdateFailed
        })?;

        if !self.is_update_required(package_version).await? {
            return Ok(CheckSuccess::UpdateNotNeeded);
        }

        if let Some(client_end) = monitor {
            self.send_on_update_started(client_end).map_err(|e| {
                fx_log_warn!("Failed to send on update start: {:#}", anyhow!(e));
                CheckError::UpdateFailed
            })?;
        }

        let proxy = connect_to_service::<InstallerMarker>().map_err(|e| {
            fx_log_warn!("Failed to connect to update installer: {:#}", anyhow!(e));
            CheckError::UpdateFailed
        })?;

        self.install_update(pinned_update_url, proxy).await.map_err(|e| {
            fx_log_warn!("Failed to install update: {:#}", anyhow!(e));
            CheckError::UpdateFailed
        })?;

        Ok(CheckSuccess::UpdatePerformed)
    }

    async fn install_update(
        &self,
        update_url: PkgUrl,
        installer: InstallerProxy,
    ) -> Result<(), Error> {
        let mut url = fidl_fuchsia_pkg::PackageUrl { url: update_url.to_string() };
        let options = Options {
            initiator: Some(Initiator::User),
            allow_attach_to_existing_attempt: Some(false),
            should_write_recovery: Some(true),
        };

        let (controller, controller_remote) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>()
                .context("Creating reboot controller")?;

        let (monitor_client, mut monitor) =
            fidl::endpoints::create_request_stream::<MonitorMarker>()
                .context("Creating update monitor")?;

        installer
            .start_update(&mut url, options, monitor_client, Some(controller_remote))
            .await
            .context("Sending start update request")?
            .map_err(|e| anyhow!("Failed starting update: {:?}", e))?;
        controller.detach().context("Detaching the reboot controller")?;

        while let Some(request) = monitor.try_next().await.context("Getting monitor event")? {
            match request {
                MonitorRequest::OnState { state, responder } => {
                    responder.send().context("Sending monitor ack")?;
                    match state {
                        State::Complete(_) | State::DeferReboot(_) => {
                            fx_log_info!("Update complete!");
                            return Ok(());
                        }
                        State::Reboot(_) => {
                            fx_log_err!(
                                "The system updater is rebooting, even though we asked it not to!"
                            );
                            return Ok(());
                        }
                        State::FailPrepare(_) | State::FailFetch(_) | State::FailStage(_) => {
                            fx_log_warn!("The update installation failed.");
                            return Err(anyhow!("Failed to install update"));
                        }
                        State::Prepare(_)
                        | State::Fetch(_)
                        | State::Stage(_)
                        | State::WaitToReboot(_) => {}
                    }
                }
            }
        }

        // Fail closed, so that we don't reboot mid-update.
        fx_log_warn!("System updater closed monitor connection before the update finished.");
        return Err(anyhow!("Update monitor stopped receiving events"));
    }

    fn send_on_update_started(
        &self,
        client: fidl::endpoints::ClientEnd<UsbMonitorMarker>,
    ) -> Result<(), Error> {
        let proxy = client.into_proxy().context("Creating monitor proxy")?;
        proxy.on_update_started().context("Sending update start")?;
        Ok(())
    }

    async fn is_update_required(&self, new_version: SystemVersion) -> Result<bool, CheckError> {
        let system_version = self.get_system_version().await.map_err(|e| {
            fx_log_warn!("Failed to get system version: {:#}", anyhow!(e));
            CheckError::UpdateFailed
        })?;

        match new_version.partial_cmp(&system_version) {
            None => {
                fx_log_warn!(
                    "Could not compare system version {} with update package version {}",
                    system_version,
                    new_version
                );
                Err(CheckError::UpdateFailed)
            }
            Some(Ordering::Greater) => Ok(true),
            Some(_) => Ok(false),
        }
    }

    fn validate_url(&self, url: PkgUrl) -> Result<PkgUrl, CheckError> {
        if url.package_hash().is_some() {
            return Err(CheckError::InvalidUpdateUrl);
        }

        Ok(url)
    }

    async fn get_system_version(&self) -> Result<SystemVersion, Error> {
        let string = io_util::file::read_in_namespace_to_string(self.build_info_path)
            .await
            .context("Reading build info")?;

        Ok(SystemVersion::from_str(&string).context("Parsing system version")?)
    }

    /// Resolve the update package at "url" using the system resolver.
    /// Returns a wrapper around the update package and the input 'url' which is now pinned to the
    /// resolved UpdatePackage.
    async fn open_update_package(&self, url: PkgUrl) -> Result<(UpdatePackage, PkgUrl), Error> {
        let resolver = connect_to_service::<PackageResolverMarker>()
            .context("Connecting to package resolver")?;
        self.open_update_package_at(url, resolver).await
    }

    /// Resolve the update package at "url" using the given resolver.
    /// Returns a wrapper around the update package and a pinned PkgUrl.
    async fn open_update_package_at(
        &self,
        url: PkgUrl,
        resolver: PackageResolverProxy,
    ) -> Result<(UpdatePackage, PkgUrl), Error> {
        let mut policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };

        let (dir, remote) = fidl::endpoints::create_proxy::<DirectoryMarker>()
            .context("Creating directory proxy")?;
        resolver
            .resolve(&url.to_string(), &mut vec![].into_iter(), &mut policy, remote)
            .await
            .context("Sending FIDL request")?
            .map_err(Status::from_raw)
            .context("Resolving pacakge")?;

        let meta_proxy = io_util::open_file(
            &dir,
            std::path::Path::new("meta"),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )
        .context("Opening meta in update package")?;
        let hash =
            io_util::file::read_to_string(&meta_proxy).await.context("Reading package hash")?;

        let hash = fuchsia_url::Hash::from_str(&hash).context("Parsing hash")?;
        let url = PkgUrl::new_package(url.host().to_string(), url.path().to_string(), Some(hash))
            .context("Creating pinned package URL")?;

        Ok((UpdatePackage::new(dir), url))
    }
}
