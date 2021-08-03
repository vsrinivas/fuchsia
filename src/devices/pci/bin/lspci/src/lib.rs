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
#[derive(FromArgs)]
/// Display PCI information
pub struct Args {
    #[argh(positional, default = "String::from(\"/dev/sys/platform/PCI0/bus\")")]
    /// path to the fuchsia.hardware.pci service
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
    #[argh(option, short = 's')]
    /// [[<bus>]:][slot][.[<func>]]    Show only devices in selected slots
    pub filter: Option<filter::Filter>,
}
