// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "start",
    example = "To start a component inside starnix:

    $ ffx starnix start <url>",
    description = "Start a component inside starnix"
)]

pub struct StartStarnixCommand {
    #[argh(positional)]
    pub url: String,
}
