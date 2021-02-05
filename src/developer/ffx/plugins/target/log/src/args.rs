// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "log", description = "")]
pub struct LogCommand {
    #[argh(subcommand)]
    pub cmd: LogSubCommand,
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
#[argh(subcommand)]
pub enum LogSubCommand {
    Watch(WatchCommand),
    Dump(DumpCommand),
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Watches for and prints logs from a target. Optionally dumps recent logs first.
#[argh(subcommand, name = "watch")]
pub struct WatchCommand {
    #[argh(option, default = "true")]
    /// if true, dumps recent logs before printing new ones.
    pub dump: bool,
}
#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Dumps all logs from a target.
#[argh(subcommand, name = "dump")]
pub struct DumpCommand {}
