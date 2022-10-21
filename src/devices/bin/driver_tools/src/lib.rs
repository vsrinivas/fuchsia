// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(not(target_os = "fuchsia"))]
#[macro_use]
extern crate lazy_static;

pub mod args;
mod subcommands;

use {
    anyhow::{Context, Result},
    args::{DriverCommand, DriverSubCommand},
    driver_connector::DriverConnector,
    std::io,
};

#[cfg(not(target_os = "fuchsia"))]
use {futures::lock::Mutex, std::sync::Arc};

pub async fn driver(cmd: DriverCommand, driver_connector: impl DriverConnector) -> Result<()> {
    match cmd.subcommand {
        #[cfg(not(target_os = "fuchsia"))]
        DriverSubCommand::Conformance(_subcmd) => {
            conformance_lib::conformance(_subcmd, &driver_connector)
                .await
                .context("Conformance subcommand failed")?;
        }
        DriverSubCommand::DebugBind(subcmd) => {
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
        DriverSubCommand::Device(subcmd) => {
            let dev = driver_connector
                .get_dev_proxy(subcmd.select)
                .await
                .context("Failed to get dev proxy")?;
            subcommands::device::device(subcmd, dev).await.context("Device subcommand failed")?;
        }
        DriverSubCommand::Dump(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::dump::dump(subcmd, &mut io::stdout(), driver_development_proxy)
                .await
                .context("Dump subcommand failed")?;
        }
        #[cfg(not(target_os = "fuchsia"))]
        DriverSubCommand::I2c(ref subcmd) => {
            let dev =
                driver_connector.get_dev_proxy(false).await.context("Failed to get dev proxy")?;
            subcommands::i2c::i2c(subcmd, &mut io::stdout(), &dev)
                .await
                .context("I2C subcommand failed")?;
        }
        DriverSubCommand::List(subcmd) => {
            let mut writer = io::stdout();
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::list::list(subcmd, &mut writer, driver_development_proxy)
                .await
                .context("List subcommand failed")?;
        }
        DriverSubCommand::ListDevices(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::list_devices::list_devices(subcmd, driver_development_proxy)
                .await
                .context("List-devices subcommand failed")?;
        }
        DriverSubCommand::ListHosts(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::list_hosts::list_hosts(subcmd, driver_development_proxy)
                .await
                .context("List-hosts subcommand failed")?;
        }
        #[cfg(not(target_os = "fuchsia"))]
        DriverSubCommand::Lsblk(subcmd) => {
            let dev = driver_connector
                .get_dev_proxy(subcmd.select)
                .await
                .context("Failed to get dev proxy")?;
            subcommands::lsblk::lsblk(subcmd, dev).await.context("Lsblk subcommand failed")?;
        }
        #[cfg(not(target_os = "fuchsia"))]
        DriverSubCommand::Lspci(subcmd) => {
            let dev = driver_connector
                .get_dev_proxy(subcmd.select)
                .await
                .context("Failed to get dev proxy")?;
            subcommands::lspci::lspci(subcmd, dev).await.context("Lspci subcommand failed")?;
        }
        #[cfg(not(target_os = "fuchsia"))]
        DriverSubCommand::Lsusb(subcmd) => {
            let device_watcher_proxy = driver_connector
                .get_device_watcher_proxy()
                .await
                .context("Failed to get device watcher proxy")?;
            subcommands::lsusb::lsusb(subcmd, device_watcher_proxy)
                .await
                .context("Lsusb subcommand failed")?;
        }
        #[cfg(not(target_os = "fuchsia"))]
        DriverSubCommand::PrintInputReport(ref subcmd) => {
            let writer = Arc::new(Mutex::new(io::stdout()));
            let dev =
                driver_connector.get_dev_proxy(false).await.context("Failed to get dev proxy")?;
            subcommands::print_input_report::print_input_report(subcmd, writer, dev)
                .await
                .context("Print-input-report subcommand failed")?;
        }
        DriverSubCommand::Register(subcmd) => {
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
        DriverSubCommand::Restart(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::restart::restart(subcmd, &mut io::stdout(), driver_development_proxy)
                .await
                .context("Restart subcommand failed")?;
        }
        #[cfg(not(target_os = "fuchsia"))]
        DriverSubCommand::RunTool(subcmd) => {
            let tool_runner_proxy = driver_connector
                .get_tool_runner_proxy(false)
                .await
                .context("Failed to get tool runner proxy")?;
            subcommands::runtool::run_tool(subcmd, &mut io::stdout(), tool_runner_proxy)
                .await
                .context("RunTool subcommand failed")?;
        }
        DriverSubCommand::TestNode(subcmd) => {
            let driver_development_proxy = driver_connector
                .get_driver_development_proxy(subcmd.select)
                .await
                .context("Failed to get driver development proxy")?;
            subcommands::test_node::test_node(&subcmd, driver_development_proxy)
                .await
                .context("AddTestNode subcommand failed")?;
        }
    };
    Ok(())
}
