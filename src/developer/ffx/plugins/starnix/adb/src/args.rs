// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "adb",
    example = "ffx starnix adb",
    description = "Bridge from host adb to adbd running inside starnix"
)]

pub struct AdbStarnixCommand {
    /// the galaxy in which to connect to adb
    #[argh(option, short = 'g', default = "String::from(\"starbionic\")")]
    pub galaxy: String,
}
