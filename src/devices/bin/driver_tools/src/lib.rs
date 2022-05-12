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
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
};

pub async fn driver(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: DriverCommand,
) -> Result<()> {
    match cmd.subcommand {
        DriverSubcommand::DebugBind(subcmd) => {
            subcommands::debug_bind::debug_bind(remote_control, subcmd)
                .await
                .context("Debug-bind subcommand failed")?;
        }
        DriverSubcommand::Device(subcmd) => {
            subcommands::device::device(remote_control, subcmd)
                .await
                .context("Device subcommand failed")?;
        }
        DriverSubcommand::Dump(subcmd) => {
            subcommands::dump::dump(remote_control, subcmd)
                .await
                .context("Dump subcommand failed")?;
        }
        DriverSubcommand::List(subcmd) => {
            subcommands::list::list(remote_control, subcmd)
                .await
                .context("List subcommand failed")?;
        }
        DriverSubcommand::ListDevices(subcmd) => {
            subcommands::list_devices::list_devices(remote_control, subcmd)
                .await
                .context("List-devices subcommand failed")?;
        }
        DriverSubcommand::ListHosts(subcmd) => {
            subcommands::list_hosts::list_hosts(remote_control, subcmd)
                .await
                .context("List-hosts subcommand failed")?;
        }
        DriverSubcommand::Lsblk(subcmd) => {
            subcommands::lsblk::lsblk(remote_control, subcmd)
                .await
                .context("Lsblk subcommand failed")?;
        }
        DriverSubcommand::Lspci(subcmd) => {
            subcommands::lspci::lspci(remote_control, subcmd)
                .await
                .context("Lspci subcommand failed")?;
        }
        DriverSubcommand::Lsusb(subcmd) => {
            subcommands::lsusb::lsusb(remote_control, subcmd)
                .await
                .context("Lsusb subcommand failed")?;
        }
        DriverSubcommand::Register(subcmd) => {
            subcommands::register::register(remote_control, subcmd)
                .await
                .context("Register subcommand failed")?;
        }
        DriverSubcommand::Restart(subcmd) => {
            subcommands::restart::restart(remote_control, subcmd)
                .await
                .context("Restart subcommand failed")?;
        }
        DriverSubcommand::RunTool(subcmd) => {
            subcommands::runtool::run_tool(remote_control, subcmd)
                .await
                .context("RunTool subcommand failed")?;
        }
    };
    Ok(())
}
