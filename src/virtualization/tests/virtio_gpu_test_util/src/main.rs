// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod framebuffer;

#[cfg(target_os = "linux")]
mod linuxfb;
#[cfg(target_os = "fuchsia")]
mod zirconfb;

use {crate::framebuffer::Framebuffer, anyhow::Error, argh::FromArgs, serde_json};

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

#[cfg(target_os = "linux")]
fn create_framebuffer() -> crate::linuxfb::LinuxFramebuffer {
    crate::linuxfb::LinuxFramebuffer
}

#[cfg(target_os = "fuchsia")]
fn create_framebuffer() -> crate::zirconfb::ZirconFramebuffer {
    crate::zirconfb::ZirconFramebuffer
}

fn detect_displays() -> Result<(), Error> {
    let info = create_framebuffer().detect_displays();
    println!("{}", serde_json::to_string_pretty(&info)?);
    Ok(())
}

fn main() -> Result<(), Error> {
    let options: Args = argh::from_env();
    match options.subcommand {
        SubCommand::Detect(_) => detect_displays(),
    }
}
