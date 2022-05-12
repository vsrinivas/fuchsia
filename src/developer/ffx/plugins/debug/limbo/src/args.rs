// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Options for "ffx debug limbo".
#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "limbo", description = "control the process limbo on the target")]
pub struct LimboCommand {
    #[argh(subcommand)]
    pub command: LimboSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum LimboSubCommand {
    Status(LimboStatusCommand),
    Enable(LimboEnableCommand),
    Disable(LimboDisableCommand),
    List(LimboListCommand),
    Release(LimboReleaseCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "status", description = "query the status of the process limbo.")]
pub struct LimboStatusCommand {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "enable",
    description = "enable the process limbo. It will now begin to capture crashing processes."
)]
pub struct LimboEnableCommand {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "disable",
    description = "disable the process limbo. Will free any pending processes waiting in it."
)]
pub struct LimboDisableCommand {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "list",
    description = "lists the processes currently waiting on limbo. The limbo must be active."
)]
pub struct LimboListCommand {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "release",
    description = "release a process from limbo. The limbo must be active."
)]
pub struct LimboReleaseCommand {
    #[argh(positional)]
    /// the koid of the process to release.
    pub pid: u64,
}
