// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "debug", description = "Start a debugging session.")]
pub struct DebugCommand {
    #[argh(subcommand)]
    pub sub_command: Option<DebugSubCommand>,
    /// path of the Unix socket used to connect to the debug agent.
    /// Defaults to /tmp/debug_agent.socket
    #[argh(positional, default = "String::from(\"/tmp/debug_agent.socket\")")]
    pub socket_location: String,
}

/// "ffx debug" sub commands.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum DebugSubCommand {
    Fidlcat(FidlcatSubCommand),
    Zxdb(ZxdbSubCommand),
}

/// Options for "ffx debug fidl".
#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "fidl",
    description = "uses fidlcat to automatically manage breakpoints to trace the fidl messages and/or the syscalls"
)]
pub struct FidlcatSubCommand {
    /// specifies the source.
    /// Source can be:
    ///
    /// device: this is the default input. The input comes from the live monitoring of one or
    /// several processes. At least one of '--remote-pid', '--remote-name', '--remote-job-id',
    /// --'remote-job-name', 'run' must be specified.
    ///
    /// <path>: playback. Used to replay a session previously recorded with --to <path>
    /// (protobuf format). Path gives the name of the file to read. If path is '-' then the standard
    /// input is used.
    ///
    /// This option must be used at most once.
    #[argh(option)]
    pub from: Option<String>,

    /// the <name> of a process.
    /// Fidlcat will monitor all existing and future processes whose names includes <name>
    /// (<name> is a substring of the process name).
    ///
    /// This option can be specified multiple times.
    ///
    /// When used with --remote-job-id or --remote-job-name, only the processes from the selected
    /// jobs are taken into account.
    #[argh(option)]
    pub remote_name: Vec<String>,
}

/// Options for "ffx debug zxdb".
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "zxdb", description = "launches zxdb to debug programs")]
pub struct ZxdbSubCommand {}
