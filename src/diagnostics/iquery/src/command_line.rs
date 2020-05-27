// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::*, types::*},
    argh::FromArgs,
    async_trait::async_trait,
    serde_json,
};

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    List(ListCommand),
    Selectors(SelectorsCommand),
    Show(ShowCommand),
    ShowFile(ShowFileCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Top-level command.
pub struct CommandLine {
    #[argh(option, default = "Format::Json")]
    /// the format to be used to display the results (json, text).
    pub format: Format,

    #[argh(subcommand)]
    pub command: SubCommand,
}

// Once Generic Associated types are implemented we could have something along the lines of
// `type Result = Box<T: Serialize>` and not rely on this macro.
macro_rules! execute_and_format {
    ($self:ident, [$($command:ident),*]) => {
        match &$self.command {
            $(
                SubCommand::$command(command) => {
                    let result = command.execute().await?;
                    match $self.format {
                        Format::Json => {
                            let json_string = serde_json::to_string_pretty(&result)
                                .map_err(|e| Error::InvalidCommandResponse(e))?;
                            println!("{}", json_string);
                        }
                        Format::Text => {
                            // TODO(fxbug.dev/45458): implement
                            unimplemented!()
                        }
                    }
                }
            )*
            _ => {
                // TODO(fxbug.dev/45458): implement the rest of commands
                unimplemented!()
            }
        }
    }
}

#[async_trait]
impl Command for CommandLine {
    type Result = ();

    async fn execute(&self) -> Result<Self::Result, Error> {
        execute_and_format!(self, [Show]);
        Ok(())
    }
}
