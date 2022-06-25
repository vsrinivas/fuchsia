// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "input",
    description = "Config input feature",
    example = "To enable / disable touchpad_mode:

    $ ffx component input --enable touchpad_mode
    $ ffx component input --disable touchpad_mode"
)]
pub struct ComponentInputCommand {
    #[argh(option, long = "enable")]
    /// enable a feature.
    pub enable: Option<String>,

    #[argh(option, long = "disable")]
    /// disable a feature.
    pub disable: Option<String>,
}
