// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{metrics, paver},
    anyhow::{Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_hardware_power_statecontrol::{
        AdminMarker as PowerStateControlMarker, AdminProxy as PowerStateControlProxy,
    },
    fidl_fuchsia_io as fio,
    fidl_fuchsia_paver::{BootManagerProxy, DataSinkProxy},
    fidl_fuchsia_pkg::{PackageCacheProxy, PackageResolverProxy, RetainedPackagesProxy},
    fidl_fuchsia_space::ManagerProxy as SpaceManagerProxy,
    fuchsia_component::client::connect_to_protocol,
    futures::{future::BoxFuture, prelude::*},
};

/// A trait to provide the ability to create a metrics client.
pub trait CobaltConnector {
    /// Create a new metrics client and return a future that completes when all events are flushed
    /// to the service.
    fn connect(&self) -> (metrics::Client, BoxFuture<'static, ()>);
}

/// A trait to provide access to /config/build-info.
#[async_trait]
pub trait BuildInfo {
    /// Read the current board name, returning None if the file does not exist.
    async fn board(&self) -> Result<Option<String>, Error>;
    /// Read the current version, returning None if the file does not exist.
    async fn version(&self) -> Result<Option<String>, Error>;
}

/// A trait to provide access to the current system image hash.
#[async_trait]
pub trait SystemInfo {
    /// Get the hash of the current system_image package, if there is one.
    async fn system_image_hash(&self) -> Result<Option<fuchsia_hash::Hash>, Error>;
}

pub trait EnvironmentConnector {
    fn connect() -> Result<Environment, Error>;
}

pub struct NamespaceEnvironmentConnector;
impl EnvironmentConnector for NamespaceEnvironmentConnector {
    fn connect() -> Result<Environment, Error> {
        Environment::connect_in_namespace()
    }
}

/// The collection of external data files and services an update attempt will utilize to perform
/// the update.
pub struct Environment<
    B = NamespaceBuildInfo,
    C = NamespaceCobaltConnector,
    S = NamespaceSystemInfo,
> {
    pub data_sink: DataSinkProxy,
    pub boot_manager: BootManagerProxy,
    pub pkg_resolver: PackageResolverProxy,
    pub pkg_cache: PackageCacheProxy,
    pub retained_packages: RetainedPackagesProxy,
    pub space_manager: SpaceManagerProxy,
    pub power_state_control: PowerStateControlProxy,
    pub build_info: B,
    pub cobalt_connector: C,
    pub system_info: S,
}

impl Environment {
    /// Opens connections to the various serrvices provided by the environment needed by a system
    /// update installation attempt. While this method can detect missing services, it is possible
    /// for this method to succeed but for the first interaction with a service to observe that the
    /// connection has since been closed.
    fn connect_in_namespace() -> Result<Self, Error> {
        let (data_sink, boot_manager) = paver::connect_in_namespace()?;
        Ok(Self {
            data_sink,
            boot_manager,
            pkg_resolver: connect_to_protocol::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .context("connect to fuchsia.pkg.PackageResolver")?,
            pkg_cache: connect_to_protocol::<fidl_fuchsia_pkg::PackageCacheMarker>()
                .context("connect to fuchsia.pkg.PackageCache")?,
            retained_packages: connect_to_protocol::<fidl_fuchsia_pkg::RetainedPackagesMarker>()
                .context("connect to fuchsia.pkg.RetainedPackages")?,
            space_manager: connect_to_protocol::<fidl_fuchsia_space::ManagerMarker>()
                .context("connect to fuchsia.space.Manager")?,
            power_state_control: connect_to_protocol::<PowerStateControlMarker>()
                .context("connect to fuchsia.hardware.power.statecontrol.Admin")?,
            build_info: NamespaceBuildInfo,
            cobalt_connector: NamespaceCobaltConnector,
            system_info: NamespaceSystemInfo,
        })
    }
}

#[derive(Debug)]
pub struct NamespaceCobaltConnector;

impl CobaltConnector for NamespaceCobaltConnector {
    fn connect(&self) -> (metrics::Client, BoxFuture<'static, ()>) {
        let (cobalt, forwarder_task) = metrics::connect_to_cobalt();
        (cobalt, forwarder_task.boxed())
    }
}

#[derive(Debug)]
pub struct NamespaceBuildInfo;

impl NamespaceBuildInfo {
    async fn read_file(&self, name: &str) -> Result<Option<String>, Error> {
        let build_info = fuchsia_fs::directory::open_in_namespace(
            "/config/build-info",
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .context("while opening /config/build-info")?;

        let file = match fuchsia_fs::directory::open_file(
            &build_info,
            name,
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .await
        {
            Ok(file) => file,
            Err(fuchsia_fs::node::OpenError::OpenError(fuchsia_zircon::Status::NOT_FOUND)) => {
                return Ok(None)
            }
            Err(e) => {
                return Err(e).with_context(|| format!("while opening /config/build-info/{}", name))
            }
        };

        let contents = fuchsia_fs::file::read_to_string(&file)
            .await
            .with_context(|| format!("while reading /config/build-info/{}", name))?;
        Ok(Some(contents))
    }
}

#[async_trait]
impl BuildInfo for NamespaceBuildInfo {
    async fn board(&self) -> Result<Option<String>, Error> {
        self.read_file("board").await
    }
    async fn version(&self) -> Result<Option<String>, Error> {
        self.read_file("version").await
    }
}

#[derive(Debug)]
pub struct NamespaceSystemInfo;

#[async_trait]
impl SystemInfo for NamespaceSystemInfo {
    async fn system_image_hash(&self) -> Result<Option<fuchsia_hash::Hash>, Error> {
        let proxy = if let Ok(proxy) = fuchsia_fs::directory::open_in_namespace(
            "/pkgfs/system",
            fio::OpenFlags::RIGHT_READABLE,
        ) {
            proxy
        } else {
            // system-updater will always have /pkgfs/system in its namespace because its manifest
            // requests it, but on configurations that do not have a system_image package it will
            // not be routed a directory, so we can't just check for NOT_FOUND.
            return Ok(None);
        };
        fuchsia_pkg::PackageDirectory::from_proxy(proxy)
            .merkle_root()
            .await
            .context("reading hash of system_image package")
            .map(Some)
    }
}

#[cfg(test)]
pub(crate) struct FakeSystemInfo(pub(crate) Option<fuchsia_hash::Hash>);

#[cfg(test)]
#[async_trait]
impl SystemInfo for FakeSystemInfo {
    async fn system_image_hash(&self) -> Result<Option<fuchsia_hash::Hash>, Error> {
        Ok(self.0)
    }
}
