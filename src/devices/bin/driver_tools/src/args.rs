// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::subcommands::{
        debug_bind::args::DebugBindCommand, device::args::DeviceCommand, dump::args::DumpCommand,
        list::args::ListCommand, list_devices::args::ListDevicesCommand,
        list_hosts::args::ListHostsCommand, lsblk::args::LsblkCommand, lspci::args::LspciCommand,
        lsusb::args::LsusbCommand, register::args::RegisterCommand, restart::args::RestartCommand,
    },
    argh::FromArgs,
};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(name = "driver", description = "Support driver development workflows")]
pub struct DriverCommand {
    #[argh(subcommand)]
    pub subcommand: DriverSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum DriverSubcommand {
    DebugBind(DebugBindCommand),
    Device(DeviceCommand),
    Dump(DumpCommand),
    List(ListCommand),
    ListDevices(ListDevicesCommand),
    ListHosts(ListHostsCommand),
    Lsblk(LsblkCommand),
    Lspci(LspciCommand),
    Lsusb(LsusbCommand),
    Register(RegisterCommand),
    Restart(RestartCommand),
}
