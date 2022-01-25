// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, argh::FromArgs, display_utils::Controller, fuchsia_async as fasync};

mod commands;

/// Top-level list of this tool's command-line arguments
#[derive(FromArgs)]
struct Args {
    #[argh(subcommand)]
    cmd: SubCommands,
}

/// Show information about all currently attached displays
#[derive(FromArgs)]
#[argh(subcommand, name = "info")]
struct InfoArgs {
    /// ID of the display to show
    #[argh(positional)]
    id: Option<u64>,

    /// show the raw FIDL structure contents
    #[argh(switch)]
    fidl: bool,
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum SubCommands {
    Info(InfoArgs),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let controller = Controller::init().await?;

    match args.cmd {
        SubCommands::Info(args) => {
            commands::show_display_info(&controller, args.id, args.fidl);
        }
    }

    Ok(())
}
