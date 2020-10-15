// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{facade::DiagnosticsFacade, types::*},
        server::Facade,
    },
    anyhow::Error,
    async_trait::async_trait,
    serde_json::Value,
};

#[async_trait(?Send)]
impl Facade for DiagnosticsFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            DiagnosticsMethod::SnapshotInspect => {
                let parsed_args: SnapshotInspectArgs = serde_json::from_value(args)?;
                self.snapshot_inspect(parsed_args).await
            }
        }
    }
}
