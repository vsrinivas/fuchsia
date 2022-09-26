// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_profile_temperature_sub_command::SubCommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "temperature", description = "Access temperature-related information")]
/// Top-level command for "ffx profile temperature".
pub struct TemperatureCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
