// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bridge;
pub mod capability;
pub mod config;
pub mod db;
pub mod device;
pub mod filter;
pub mod util;

use argh::FromArgs;
#[derive(FromArgs, Default)]
/// Display PCI information
pub struct Args {
    #[argh(positional)]
    /// [[<bus>]:][slot][.[<func>]]    Show only devices in selected slots
    pub filter: Option<filter::Filter>,

    #[argh(option, default = "String::from(\"/dev/sys/platform/platform-passthrough/\")")]
    /// path to the parent directory of the fuchsia.hardware.pci service
    pub service: String,

    #[argh(switch, short = 'v')]
    /// print verbose device configuration
    pub verbose: bool,

    #[argh(switch, short = 'q')]
    /// don't print errors found trying to parse the database
    pub quiet: bool,

    #[argh(switch, short = 'x')]
    /// dump raw configuration space
    pub print_config: bool,

    #[argh(switch, short = 'n')]
    /// print numeric IDs.
    pub print_numeric: bool,

    #[argh(switch, short = 'N')]
    /// only print numeric IDs.
    pub only_print_numeric: bool,

    #[argh(subcommand)]
    pub command: Option<SubCommand>,
}

#[derive(Copy, Clone, FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    Buses(BusesCommand),
    Read(ReadBarCommand),
}

/// List PCI buses found in the system.
#[derive(Copy, Clone, FromArgs, PartialEq, Default, Debug)]
#[argh(subcommand, name = "buses")]
pub struct BusesCommand {}

/// Read from an MMIO BAR of a specified device.
/// For example, to read from BAR 2 of device at address 00:01.0:
///   lspci read 00:01.0 2
#[derive(Copy, Clone, FromArgs, PartialEq, Default, Debug)]
#[argh(subcommand, name = "read")]
pub struct ReadBarCommand {
    /// device address in BDF format BB:DD.F.
    #[argh(positional)]
    pub device: filter::Filter,
    /// BAR id to read from.
    #[argh(positional)]
    pub bar_id: u8,
    /// offset into the BAR to read [default = 0x0].
    #[argh(option, short = 'o', default = "0")]
    pub offset: u64,
    /// how much to read [default = 0x80].
    #[argh(option, short = 's', default = "128")]
    pub size: u64,
}
