// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_emulator_sub_command::Subcommand};

/// entry point for ffx
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "emu", description = "Start and manage Fuchsia emulators")]
pub struct EmulatorCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
