// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;

mod backlight;
mod panel;
mod utils;

/// Top-level command line arguments.
#[derive(FromArgs, Debug, PartialEq)]
struct TopLevelArgs {
    #[argh(subcommand)]
    command: TweakCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
enum TweakCommand {
    Backlight(backlight::BacklightCmd),
    Panel(panel::PanelCmd),
}

#[fuchsia::main(logging = true, logging_tags = ["display-tweak"])]
async fn main() -> Result<(), Error> {
    let args: TopLevelArgs = argh::from_env();

    let command_result = match args.command {
        TweakCommand::Backlight(backlight_cmd) => backlight_cmd.exec().await,
        TweakCommand::Panel(panel_cmd) => panel_cmd.exec().await,
    };

    command_result
}
