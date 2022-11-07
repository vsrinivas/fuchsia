// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::types::*, types::Error},
    argh::FromArgs,
    async_trait::async_trait,
};

/// Lists all ArchiveAccessor files under the provided paths. If no paths are provided, it'll list
/// under the current directory. At the moment v2 components cannot be seen through the filesystem.
/// Therefore this only outputs ArchiveAccessors exposed by v1 components.
#[derive(Default, FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list-accessors")]
pub struct ListAccessorsCommand {
    #[argh(positional)]
    /// glob paths from where to list files.
    pub paths: Vec<String>,
}

#[async_trait]
impl Command for ListAccessorsCommand {
    type Result = Vec<String>;

    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error> {
        let paths = provider.get_accessor_paths(&self.paths).await?;
        Ok(paths)
    }
}
