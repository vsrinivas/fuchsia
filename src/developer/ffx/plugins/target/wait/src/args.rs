// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "wait",
    description = "Wait until the target becomes available.",
    error_code(1, "Timeout while getting ssh address")
)]

pub struct WaitCommand {
    #[argh(option, short = 't', default = "60")]
    /// the timeout in seconds [default = 60]
    pub timeout: usize,
}
