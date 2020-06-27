// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "powerctl", description = "reboots a device. ")]
pub struct PowerCtlCommand {
    #[argh(subcommand)]
    pub ctl_type: PowerCtlSubcommand,
}

#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "reboot", description = "reboots a device. ")]
pub struct RebootCommand {}

#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "bootloader",
    description = "reboots a device into the bootloader state. "
)]
pub struct BootloaderCommand {}

#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "recovery", description = "reboots a device into recovery. ")]
pub struct RecoveryCommand {}

#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "poweroff", description = "powers off a device. ")]
pub struct PoweroffCommand {}

#[derive(FromArgs, PartialEq, Debug, Clone)]
#[argh(subcommand)]
pub enum PowerCtlSubcommand {
    Reboot(RebootCommand),
    Poweroff(PoweroffCommand),
    Bootloader(BootloaderCommand),
    Recovery(RecoveryCommand),
}
