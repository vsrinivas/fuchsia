// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::types::*,
        types::{Error, ToText},
    },
    argh::FromArgs,
    async_trait::async_trait,
    diagnostics_data::{Logs, LogsData},
};

/// Prints the logs.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "logs")]
pub struct LogsCommand {
    #[argh(option)]
    /// A selector specifying what `fuchsia.diagnostics.ArchiveAccessor` to connect to.
    /// The selector will be in the form of:
    /// <moniker>:<directory>:fuchsia.diagnostics.ArchiveAccessorName
    ///
    /// Typically this is the output of `iquery list-accessors`.
    ///
    /// For example: `bootstrap/archivist:expose:fuchsia.diagnostics.FeedbackArchiveAccessor`
    /// means that the command will connect to the `FeedbackArchiveAccecssor`
    /// exposed by `bootstrap/archivist`.
    pub accessor: Option<String>,
}

impl ToText for Vec<LogsData> {
    fn to_text(self) -> String {
        self.into_iter().map(|log| format!("{}", log)).collect::<Vec<_>>().join("\n")
    }
}

#[async_trait]
impl Command for LogsCommand {
    type Result = Vec<LogsData>;

    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error> {
        let mut results = provider.snapshot::<Logs>(&self.accessor, &[]).await?;
        for result in results.iter_mut() {
            if let Some(hierarchy) = &mut result.payload {
                hierarchy.sort();
            }
        }
        Ok(results)
    }
}
