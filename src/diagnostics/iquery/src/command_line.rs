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
    ListAccessors(ListAccessorsCommand),
    ListFiles(ListFilesCommand),
    Selectors(SelectorsCommand),
    Show(ShowCommand),
    Logs(LogsCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Top-level command.
pub struct CommandLine {
    #[argh(option, default = "Format::Text", short = 'f')]
    /// the format to be used to display the results (json, text).
    pub format: Format,

    #[argh(subcommand)]
    pub command: SubCommand,
}

// Once Generic Associated types are implemented we could have something along the lines of
// `type Result = Box<T: Serialize>` and not rely on this macro.
macro_rules! execute_and_format {
    ($self:ident, $provider:ident, [$($command:ident),*]) => {
        match &$self.command {
            $(
                SubCommand::$command(command) => {
                    let result = command.execute($provider).await?;
                    match $self.format {
                        Format::Json => {
                            serde_json::to_string_pretty(&result)
                                .map_err(|e| Error::InvalidCommandResponse(e))
                        }
                        Format::Text => {
                            Ok(result.to_text())
                        }
                    }
                }
            )*
        }
    }
}

#[async_trait]
impl Command for CommandLine {
    type Result = String;

    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error> {
        execute_and_format!(self, provider, [List, ListAccessors, ListFiles, Logs, Selectors, Show])
    }
}
