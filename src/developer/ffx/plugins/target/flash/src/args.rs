// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "flash", description = "flashes a device")]
pub struct FlashCommand {
    //path to flashing manifest
    #[argh(positional)]
    pub manifest: String,
}
