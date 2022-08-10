// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod types;

#[cfg(target_os = "linux")]
mod linuxfb;

use {
    anyhow::{anyhow, Error},
    argh::FromArgs,
    serde_json,
};

/// Detect details about the framebuffer.
#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "detect")]
pub struct DetectArgs {}

#[derive(FromArgs, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    Detect(DetectArgs),
}

/// Top-level help
#[derive(FromArgs, Debug)]
struct Args {
    #[argh(subcommand)]
    subcommand: SubCommand,
}

fn detect_displays() -> Result<(), Error> {
    let info = if cfg!(target_os = "linux") {
        crate::linuxfb::read_dev_fb0_info()
    } else {
        return Err(anyhow!("Display detection is not implemented"));
    };
    println!("{}", serde_json::to_string_pretty(&info)?);
    Ok(())
}

fn main() -> Result<(), Error> {
    let options: Args = argh::from_env();
    match options.subcommand {
        SubCommand::Detect(_) => detect_displays(),
    }
}
