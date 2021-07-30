// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "deregister", description = "")]
pub struct DeregisterCommand {
    #[argh(option, short = 'r')]
    /// remove the repository named `name` from the target, rather than the default.
    pub repository: Option<String>,
}
