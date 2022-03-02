// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "set-preferred-ssh-address",
    description = "Sets the preferred SSH address",
    example = "To set a preferred IPv4 SSH address:

    $ ffx target set-preferred-ssh-address 127.0.0.1

Or to set a preferred IPv6 SSH address:

    $ ffx target set-preferred-ssh-addres fe80::32fd:38ff:fea8:a00a%qemu

If provided, the scope may either correspond to the numerical ID or the
interface name.",
    note = "Manually set the preferred SSH address on the default target. If
successful, then any existing connection to the target is severed and a new
connection is established. The specified address is not persisted across daemon
version changes or restarts."
)]
pub struct SetPreferredSshAddressCommand {
    #[argh(positional)]
    /// the SSH IP address to use. This must correspond to a known address on
    /// the target.
    pub addr: String,
}
