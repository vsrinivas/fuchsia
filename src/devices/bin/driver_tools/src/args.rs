// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::subcommands::{
        debug_bind::args::DebugBindCommand, device::args::DeviceCommand, dump::args::DumpCommand,
        i2c::args::I2cCommand, list::args::ListCommand, list_devices::args::ListDevicesCommand,
        list_hosts::args::ListHostsCommand, lsblk::args::LsblkCommand, lspci::args::LspciCommand,
        lsusb::args::LsusbCommand, print_input_report::args::PrintInputReportCommand,
        register::args::RegisterCommand, restart::args::RestartCommand,
        runtool::args::RunToolCommand, test_node::args::TestNodeCommand,
    },
    argh::FromArgs,
};

// Driver conformance testing is run on the host against a target device's driver.
// So, this subcommand is only relevant on the host side.
// If we are host-side, then we will import the conformance library.
// Otherwise, we will use the placeholder subcommand declared below.
#[cfg(target_os = "fuchsia")]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "conformance", description = "This command no-ops on this platform.")]
pub struct ConformanceCommand {}

#[cfg(not(target_os = "fuchsia"))]
use conformance_lib::args::ConformanceCommand;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(name = "driver", description = "Support driver development workflows")]
pub struct DriverCommand {
    #[argh(subcommand)]
    pub subcommand: DriverSubCommand,
}

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
