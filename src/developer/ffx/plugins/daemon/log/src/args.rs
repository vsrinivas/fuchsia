// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "log", description = "Dumps the daemon log")]
pub struct LogCommand {
    #[argh(switch, short = 'f', description = "print appended logs as they happen")]
    pub follow: bool,

    #[argh(option, long = "line-count", short = 'l')]
    /// display most recent log lines.
    pub line_count: Option<usize>,
}
