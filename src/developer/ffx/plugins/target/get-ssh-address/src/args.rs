// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_config::FfxConfigBacked, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FfxConfigBacked, FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get-ssh-address", description = "get target's ssh address")]
pub struct GetSshAddressCommand {
    #[argh(option, short = 't')]
    #[ffx_config_default(key = "target.interaction.timeout", default = "1.0")]
    /// sets the timeout for getting the target's SSH address. Defaults to 1.0,
    /// backed by "target.interaction.timeout" config value.
    pub timeout: Option<f64>,
}
