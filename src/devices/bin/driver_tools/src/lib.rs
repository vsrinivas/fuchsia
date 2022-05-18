// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate lazy_static;

pub mod args;
mod subcommands;

use {
    anyhow::{Context, Result},
    args::{DriverCommand, DriverSubcommand},
    fidl_fuchsia_device_manager as fdm, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_driver_playground as fdp, fidl_fuchsia_driver_registrar as fdr,
    fidl_fuchsia_io as fio,
    std::io,
};

#[async_trait::async_trait]
pub trait DriverConnector {
    async fn get_driver_development_proxy(
        &self,
        select: bool,
    ) -> Result<fdd::DriverDevelopmentProxy>;
    async fn get_dev_proxy(&self, select: bool) -> Result<fio::DirectoryProxy>;
    async fn get_device_watcher_proxy(&self) -> Result<fdm::DeviceWatcherProxy>;
    async fn get_driver_registrar_proxy(&self, select: bool) -> Result<fdr::DriverRegistrarProxy>;
    async fn get_tool_runner_proxy(&self, select: bool) -> Result<fdp::ToolRunnerProxy>;
}

pub async fn driver(cmd: DriverCommand, driver_connector: impl DriverConnector) -> Result<()> {
    match cmd.subcommand {
        DriverSubcommand::DebugBind(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::debug_bind::debug_bind(
                subcmd,
                &mut io::stdout(),
                driver_development_proxy,
            )
            .await
            .context("Debug-bind subcommand failed")?;
        }
        DriverSubcommand::Device(subcmd) => {
            let dev = driver_connector
                .get_dev_proxy(subcmd.select)
                .await
                .context("Failed to get dev proxy")?;
            subcommands::device::device(subcmd, dev).await.context("Device subcommand failed")?;
        }
        DriverSubcommand::Dump(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::dump::dump(subcmd, driver_development_proxy)
                .await
                .context("Dump subcommand failed")?;
        }
        DriverSubcommand::List(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::list::list(subcmd, driver_development_proxy)
                .await
                .context("List subcommand failed")?;
        }
        DriverSubcommand::ListDevices(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::list_devices::list_devices(subcmd, driver_development_proxy)
                .await
                .context("List-devices subcommand failed")?;
        }
        DriverSubcommand::ListHosts(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::list_hosts::list_hosts(subcmd, driver_development_proxy)
                .await
                .context("List-hosts subcommand failed")?;
        }
        DriverSubcommand::Lsblk(subcmd) => {
            let dev = driver_connector
                .get_dev_proxy(subcmd.select)
                .await
                .context("Failed to get dev proxy")?;
            subcommands::lsblk::lsblk(subcmd, dev).await.context("Lsblk subcommand failed")?;
        }
        DriverSubcommand::Lspci(subcmd) => {
            let dev = driver_connector
                .get_dev_proxy(subcmd.select)
                .await
                .context("Failed to get dev proxy")?;
            subcommands::lspci::lspci(subcmd, dev).await.context("Lspci subcommand failed")?;
        }
        DriverSubcommand::Lsusb(subcmd) => {
            let device_watcher_proxy = driver_connector
                .get_device_watcher_proxy()
                .await
                .context("Failed to get device watcher proxy")?;
            subcommands::lsusb::lsusb(subcmd, device_watcher_proxy)
                .await
                .context("Lsusb subcommand failed")?;
        }
        DriverSubcommand::Register(subcmd) => {
            let driver_registrar_proxy = driver_connector
                .get_driver_registrar_proxy(subcmd.select)
                .await
                .context("Failed to get driver registrar proxy")?;
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::register::register(
                subcmd,
                &mut io::stdout(),
                driver_registrar_proxy,
                driver_development_proxy,
            )
            .await
            .context("Register subcommand failed")?;
        }
        DriverSubcommand::Restart(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::restart::restart(subcmd, &mut io::stdout(), driver_development_proxy)
                .await
                .context("Restart subcommand failed")?;
        }
        DriverSubcommand::RunTool(subcmd) => {
            let tool_runner_proxy = driver_connector
                .get_tool_runner_proxy(false)
                .await
                .context("Failed to get tool runner proxy")?;
            subcommands::runtool::run_tool(subcmd, &mut io::stdout(), tool_runner_proxy)
                .await
                .context("RunTool subcommand failed")?;
        }
    };
    Ok(())
}
