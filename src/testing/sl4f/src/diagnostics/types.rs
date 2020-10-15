// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

/// Enum for supported Diagnostics commands.
#[derive(Debug, PartialEq)]
pub enum DiagnosticsMethod {
    /// Wraps `fuchsia.diagnostics.Archive.StreamDiagnostics` with default parameters
    /// stream_mode=SNAPSHOT, format=JSON, data_type=INSPECT,
    /// batch_retrieval_timeout_seconds=60
    SnapshotInspect,
}

#[derive(Serialize, Deserialize)]
pub struct SnapshotInspectArgs {
    pub selectors: Vec<String>,
    pub service_name: String,
}

impl std::str::FromStr for DiagnosticsMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "SnapshotInspect" => Ok(DiagnosticsMethod::SnapshotInspect),
            _ => return Err(format_err!("invalid Diagnostics Facade method: {}", method)),
        }
    }
}
