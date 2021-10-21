// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_emulator_remote_args::RemoteCommand;
use ffx_emulator_shutdown_args::ShutdownCommand;
use ffx_emulator_start_args::StartCommand;

/// entry point for fvdl
#[derive(FromArgs, Debug, PartialEq)]
/// Commands to start/stop the emulator via fuchsia virtual device launcher (VDL)
pub struct Args {
    #[argh(subcommand)]
    pub command: VDLCommand,
    /// running in fuchsia sdk (not inside the fuchsia code repository)
    #[argh(switch)]
    pub sdk: bool,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum VDLCommand {
    Start(StartCommand),
    Shutdown(ShutdownCommand),
    Remote(RemoteCommand),
}
