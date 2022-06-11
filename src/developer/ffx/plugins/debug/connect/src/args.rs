// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Options for "ffx debug connect".
#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "connect", description = "start the debugger and connect to the target")]
pub struct ConnectCommand {
    /// start zxdb in another debugger. Valid options are lldb (preferred) and gdb.
    #[argh(option)]
    pub debugger: Option<String>,

    /// only start the debug agent but not the zxdb. The path to the UNIX socket will be printed
    /// and can be connected via "connect -u" in zxdb shell.
    #[argh(switch)]
    pub agent_only: bool,

    /// do not automatically attach to processes found in Process Limbo upon successful connection.
    /// convenience switch for the zxdb option of the same name.
    #[argh(switch, short = 'n')]
    pub no_auto_attach_limbo: bool,

    /// extra arguments passed to zxdb. Any arguments starting with "-" must be after a "--" separator.
    #[argh(positional)]
    pub zxdb_args: Vec<String>,
}
