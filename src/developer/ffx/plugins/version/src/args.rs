// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "version", description = "Print out ffx tool and daemon versions")]
pub struct VersionCommand {
    #[argh(
        switch,
        short = 'v',
        description = "if true, includes details about both ffx and the daemon"
    )]
    pub verbose: bool,
}
