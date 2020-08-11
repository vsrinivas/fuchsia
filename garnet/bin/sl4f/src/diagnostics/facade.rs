// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::diagnostics::types::SnapshotInspectArgs;
use anyhow::Error;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fuchsia_component::client;
use fuchsia_inspect_contrib::reader::{ArchiveReader, DataType};
use serde_json::Value;

/// Facade providing access to diagnostics interface.
#[derive(Debug)]
pub struct DiagnosticsFacade {}

impl DiagnosticsFacade {
    pub fn new() -> DiagnosticsFacade {
        DiagnosticsFacade {}
    }

    pub async fn snapshot_inspect(&self, args: SnapshotInspectArgs) -> Result<Value, Error> {
        let service_path = format!("/svc/{}", args.service_name);
        let proxy =
            client::connect_to_service_at_path::<ArchiveAccessorMarker>(&service_path).unwrap();
        ArchiveReader::new()
            .retry_if_empty(false)
            .with_archive(proxy)
            .add_selectors(args.selectors.into_iter())
            .snapshot_raw(DataType::Inspect)
            .await
    }
}
