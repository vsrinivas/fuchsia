// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils::connect_to_archive_accessor},
        types::{Error, ToText},
    },
    argh::FromArgs,
    async_trait::async_trait,
    diagnostics_data::LogsData,
    diagnostics_reader::{ArchiveReader, Logs},
};

/// Prints the logs.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "logs")]
pub struct LogsCommand {
    #[argh(option)]
    /// the path from where to get the ArchiveAccessor connection. If the given path is a
    /// directory, the command will look for a `fuchsia.diagnostics.ArchiveAccessor` service file.
    /// If the given path is a service file, the command will attempt to connect to it as an
    /// ArchiveAccessor.
    pub accessor_path: Option<String>,
}

impl ToText for Vec<LogsData> {
    fn to_text(self) -> String {
        self.into_iter().map(|log| format!("{}", log)).collect::<Vec<_>>().join("\n")
    }
}

#[async_trait]
impl Command for LogsCommand {
    type Result = Vec<LogsData>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        let archive = connect_to_archive_accessor(&self.accessor_path).await?;
        let reader = ArchiveReader::new().with_archive(archive).retry_if_empty(false);
        let mut results = reader.snapshot::<Logs>().await.unwrap();
        for result in results.iter_mut() {
            if let Some(hierarchy) = &mut result.payload {
                hierarchy.sort();
            }
        }
        Ok(results)
    }
}
