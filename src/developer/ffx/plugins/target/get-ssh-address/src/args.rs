// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get-ssh-address", description = "get target's ssh address")]
pub struct GetSshAddressCommand {
    #[argh(
        option,
        short = 't',
        description = "how long to wait for target address in fractional seconds. Zero returns immediately (default 1.0)"
    )]
    pub timeout: Option<f64>,

    #[argh(positional)]
    pub nodename: Option<String>,
}
