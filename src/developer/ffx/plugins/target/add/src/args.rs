// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::net::IpAddr};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "add", description = "Make ffx aware of a target at a given IP address.")]
pub struct AddCommand {
    #[argh(positional)]
    /// IP of the target.
    pub addr: IpAddr,
}
