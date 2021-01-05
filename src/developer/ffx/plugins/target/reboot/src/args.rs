// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone, Copy)]
#[argh(
    subcommand,
    name = "reboot",
    description = "Reboots a target",
    note = "Reboot a target. Uses the 'fuchsia.hardware.power.statecontrol.Admin'
FIDL API to send the reboot command.

By default, target boots fully. This behavior can be overrided by passing
in either `--bootloader` or `--recovery` to boot into the bootloader or
recovery, respectively.

The 'fuchsia.hardware.power.statecontrol.Admin' is exposed via the 'appmgr'
component. To verify that the target exposes this service, `ffx component
select` or `ffx component knock` can be used.",
    error_code(1, "Timeout while powering off target.")
)]
pub struct RebootCommand {
    /// reboot to bootloader
    #[argh(switch, short = 'b')]
    pub bootloader: bool,

    /// reboot to recovery
    #[argh(switch, short = 'r')]
    pub recovery: bool,
}
