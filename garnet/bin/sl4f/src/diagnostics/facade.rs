// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::types::SnapshotInspectArgs, anyhow::Error,
    fuchsia_inspect_contrib::reader::ArchiveReader, serde_json::Value,
};

/// Facade providing access to diagnostics interface.
#[derive(Debug)]
pub struct DiagnosticsFacade {}

impl DiagnosticsFacade {
    pub fn new() -> DiagnosticsFacade {
        DiagnosticsFacade {}
    }

    pub async fn snapshot_inspect(&self, args: SnapshotInspectArgs) -> Result<Value, Error> {
        ArchiveReader::new()
            .retry_if_empty(false)
            .add_selectors(args.selectors.into_iter())
            .get_raw_json()
            .await
    }
}
