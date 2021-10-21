// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Default, Debug, PartialEq)]
#[argh(subcommand, name = "shutdown")]
/// Shutdown running Fuchsia emulator.
pub struct ShutdownCommand {
    /// device_launcher binary location. Defaults to looking in prebuilt/vdl/device_launcher
    #[argh(option, short = 'd')]
    pub vdl_path: Option<String>,
    /// required, file containing device_launcher process artifact location.
    #[argh(option)]
    pub launched_proto: Option<String>,
    /// running in fuchsia sdk (not inside the fuchsia code repository)
    #[argh(switch)]
    pub sdk: bool,
}
