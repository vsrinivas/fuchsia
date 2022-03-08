// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Options for "ffx debug core".
#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "core", description = "start the debugger and open a minidump")]
pub struct CoreCommand {
    /// minidump.dmp file to open. If left empty, the tool will list available minidumps on the
    /// device and ask for a choice to open.
    #[argh(positional)]
    pub minidump: Option<String>,

    /// extra arguments passed to zxdb.
    #[argh(option)]
    pub zxdb_args: Vec<String>,
}
