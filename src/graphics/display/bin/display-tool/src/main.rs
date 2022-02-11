// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    display_utils::Controller,
    fuchsia_async as fasync,
    futures::{
        future::{FutureExt, TryFutureExt},
        select,
    },
};

mod commands;
mod draw;
mod fps;

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

/// Show the active refresh rate for one or more displays
#[derive(FromArgs)]
#[argh(subcommand, name = "fps")]
struct FpsArgs {
    /// ID of the display to show
    #[argh(positional)]
    id: Option<u64>,
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum SubCommands {
    Info(InfoArgs),
    Fps(FpsArgs),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let controller = Controller::init().await?;

    let fidl_events_future = controller.handle_events().err_into();
    let cmd_future = async {
        match args.cmd {
            SubCommands::Info(args) => commands::show_display_info(&controller, args.id, args.fidl),
            SubCommands::Fps(args) => commands::fps(&controller, args.id).await,
        }
    };

    select! {
        result1 = fidl_events_future.fuse() => result1,
        result2 = cmd_future.fuse() => result2,
    }
}
