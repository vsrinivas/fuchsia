// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::types::{Error, ToText},
    async_trait::async_trait,
    diagnostics_data::{Data, DiagnosticsData},
    serde::Serialize,
};

#[async_trait]
pub trait Command {
    type Result: Serialize + ToText;
    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error>;
}

#[async_trait]
pub trait DiagnosticsProvider: Send + Sync {
    async fn snapshot<D: DiagnosticsData>(
        &self,
        accessor_path: &Option<String>,
        selectors: &[String],
    ) -> Result<Vec<Data<D>>, Error>;

    /// Lists all ArchiveAccessor files under the provided paths. If no paths are provided, it'll list
    /// under the current directory. At the moment v2 components cannot be seen through the filesystem.
    /// Therefore this only outputs ArchiveAccessors exposed by v1 components.
    async fn get_accessor_paths(&self, paths: &Vec<String>) -> Result<Vec<String>, Error>;
}
