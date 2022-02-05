// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Options for "ffx debug symbolize".
#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "symbolize", description = "symbolize backtraces in markup format")]
pub struct SymbolizeCommand {
    /// start the authentication process.
    #[argh(switch)]
    pub auth: bool,

    /// extra arguments passed to the symbolizer. Any arguments starting with "-" must be after a "--" separator.
    #[argh(positional)]
    pub symbolizer_args: Vec<String>,
}
