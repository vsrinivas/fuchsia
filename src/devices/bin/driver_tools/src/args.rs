// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::subcommands::{
        debug_bind::args::DebugBindCommand, device::args::DeviceCommand, dump::args::DumpCommand,
        list::args::ListCommand, list_devices::args::ListDevicesCommand,
        list_hosts::args::ListHostsCommand, register::args::RegisterCommand,
        restart::args::RestartCommand, test_node::args::TestNodeCommand,
    },
    argh::FromArgs,
};

#[cfg(not(target_os = "fuchsia"))]
use {
    super::subcommands::{
        i2c::args::I2cCommand, lsblk::args::LsblkCommand, lspci::args::LspciCommand,
        lsusb::args::LsusbCommand, print_input_report::args::PrintInputReportCommand,
        runtool::args::RunToolCommand,
    },
    // Driver conformance testing is run on the host against a target device's driver.
    // So, this subcommand is only relevant on the host side.
    // If we are host-side, then we will import the conformance library.
    // Otherwise, we will use the placeholder subcommand declared above.
    conformance_lib::args::ConformanceCommand,
};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(name = "driver", description = "Support driver development workflows")]
pub struct DriverCommand {
    #[argh(subcommand)]
    pub subcommand: DriverSubCommand,
}

#[cfg(target_os = "fuchsia")]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum DriverSubCommand {
    DebugBind(DebugBindCommand),
    Device(DeviceCommand),
    Dump(DumpCommand),
    List(ListCommand),
    ListDevices(ListDevicesCommand),
    ListHosts(ListHostsCommand),
    Register(RegisterCommand),
    Restart(RestartCommand),
    TestNode(TestNodeCommand),
}

#[cfg(not(target_os = "fuchsia"))]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum DriverSubCommand {
    Conformance(ConformanceCommand),
    DebugBind(DebugBindCommand),
    Device(DeviceCommand),
    Dump(DumpCommand),
    I2c(I2cCommand),
    List(ListCommand),
    ListDevices(ListDevicesCommand),
    ListHosts(ListHostsCommand),
    Lsblk(LsblkCommand),
    Lspci(LspciCommand),
    Lsusb(LsusbCommand),
    PrintInputReport(PrintInputReportCommand),
    Register(RegisterCommand),
    Restart(RestartCommand),
    RunTool(RunToolCommand),
    TestNode(TestNodeCommand),
}
