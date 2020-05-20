// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::*, types::*},
    argh::FromArgs,
    async_trait::async_trait,
};

#[derive(FromArgs, PartialEq, Debug)]
/// Top-level command.
pub struct Options {
    #[argh(option)]
    /// the format to be used to display the results (json, text).
    pub format: Format,

    #[argh(subcommand)]
    pub command: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    List(ListCommand),
    Selectors(SelectorsCommand),
    Show(ShowCommand),
    ShowFile(ShowFileCommand),
}

#[async_trait]
impl Command for SubCommand {
    async fn execute(&self) -> Result<(), Error> {
        match self {
            SubCommand::List(_) => {
                // TODO(fxbug.dev/45458): implement
                unimplemented!();
            }
            SubCommand::Selectors(_) => {
                // TODO(fxbug.dev/45458): implement
                unimplemented!();
            }
            SubCommand::Show(_) => {
                // TODO(fxbug.dev/45458): implement
                unimplemented!();
            }
            SubCommand::ShowFile(_) => {
                // TODO(fxbug.dev/45458): implement
                unimplemented!();
            }
        }
    }
}
