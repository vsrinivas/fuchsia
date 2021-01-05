// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_config::FfxConfigBacked, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FfxConfigBacked, FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "get-ssh-address",
    description = "Get the target's ssh address",
    note = "Return the SSH address of the default target defined in the
`target.default` key. By default this comes from the 'User Configuration'.

The command takes a <timeout> value in seconds with a default of `1.0`
and overrides the value in the `target.interaction.timeout` key.",
    error_code(1, "Timeout while getting ssh address")
)]

pub struct GetSshAddressCommand {
    #[argh(option, short = 't')]
    #[ffx_config_default(key = "target.interaction.timeout", default = "1.0")]
    /// the timeout in seconds [default = 1.0]
    pub timeout: Option<f64>,
}
