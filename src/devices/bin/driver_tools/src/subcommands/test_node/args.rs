// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::subcommands::add::args::AddTestNodeCommand,
    super::subcommands::remove::args::RemoveTestNodeCommand, argh::FromArgs,
};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "test-node", description = "Commands to interact with test nodes.")]
pub struct TestNodeCommand {
    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,

    #[argh(subcommand)]
    pub subcommand: TestNodeSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum TestNodeSubcommand {
    Add(AddTestNodeCommand),
    Remove(RemoveTestNodeCommand),
}
