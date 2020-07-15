// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone, Copy)]
#[argh(subcommand, name = "reboot", description = "reboots a device. ")]
pub struct RebootCommand {
    /// reboot to bootloader
    #[argh(switch, short = 'b')]
    pub bootloader: bool,

    /// reboot torecovery
    #[argh(switch, short = 'r')]
    pub recovery: bool,
}
