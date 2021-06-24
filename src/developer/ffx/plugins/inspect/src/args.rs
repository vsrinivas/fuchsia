// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs, ffx_core::ffx_command, ffx_inspect_sub_command::Subcommand,
    iquery::types::Format,
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "inspect",
    description = "Query component nodes exposed via the Inspect API"
)]
pub struct InspectCommand {
    #[argh(option, default = "Format::Text", short = 'f')]
    /// the format to be used to display the results (json, text).
    pub format: Format,

    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
