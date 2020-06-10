// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "cli", description = "run echo test against the cli")]
pub struct EchoCommand {
    #[argh(positional)]
    /// number of times to print
    pub times: Option<usize>,
}
