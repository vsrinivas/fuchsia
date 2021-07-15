// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "export", description = "export a package archive")]
pub struct ExportCommand {
    #[argh(positional, description = "package to export")]
    pub package: String,
    #[argh(option, description = "output", short = 'o')]
    pub output: String,
}
