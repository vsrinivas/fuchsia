// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logs::error::LogsError;
use fidl::prelude::*;
use fidl_fuchsia_diagnostics::{self, BatchIteratorControlHandle};
use fuchsia_zircon_status::Status as ZxStatus;
use std::path::PathBuf;
use thiserror::Error;
use tracing::warn;

#[derive(Debug, Error)]
pub enum Error {
    #[error(transparent)]
    Logs(#[from] LogsError),

    #[error("Failed to serve outgoing dir: {0}")]
    ServeOutgoing(#[source] anyhow::Error),

    #[error(transparent)]
    Inspect(#[from] fuchsia_inspect::Error),

    #[error("Failed to parse config at path {0}")]
    ParseConfig(PathBuf),

    #[error("Failed to parse service config at path {0}")]
    ParseServiceConfig(PathBuf),

    #[error("Encountered a diagnostics data repository node with more than one artifact container. {0:?}")]
    MultipleArtifactContainers(Vec<String>),

    #[error("Failed to match component moniker agianst selectors: {0:?}")]
    MatchComponentMoniker(#[source] anyhow::Error),

    #[error(transparent)]
    Hierarchy(#[from] diagnostics_hierarchy::Error),
}

#[derive(Debug, Error)]
pub enum AccessorError {
    #[error("data_type must be set")]
    MissingDataType,

    #[error("client_selector_configuration must be set")]
    MissingSelectors,

    #[error("no selectors were provided")]
    EmptySelectors,

    #[error("requested selectors are unsupported: {}", .0)]
    InvalidSelectors(&'static str),

    #[error("couldn't parse/validate the provided selectors: {}", .0)]
    ParseSelectors(#[from] selectors::Error),

    #[error("only selectors of type `component:root` are supported for logs at the moment")]
    InvalidLogSelector,

    #[error("format must be set")]
    MissingFormat,

    #[error("only JSON supported right now")]
    UnsupportedFormat,

    #[error("stream_mode must be set")]
    MissingMode,

    #[error("only snapshot supported right now")]
    UnsupportedMode,

    #[error("IPC failure")]
    Ipc {
        #[from]
        source: fidl::Error,
    },

    #[error("Unable to create a VMO -- extremely unusual!")]
    VmoCreate(#[source] ZxStatus),

    #[error("Unable to write to VMO -- we may be OOMing")]
    VmoWrite(#[source] ZxStatus),

    #[error("Unable to get VMO size -- extremely unusual")]
    VmoSize(#[source] ZxStatus),

    #[error("JSON serialization failure: {0}")]
    Serialization(#[from] serde_json::Error),

    #[error("batch timeout was set on StreamParameter and on PerformanceConfiguration")]
    DuplicateBatchTimeout,

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

impl AccessorError {
    pub fn close(self, control: BatchIteratorControlHandle) {
        warn!(error = %self, "Closing BatchIterator.");
        let epitaph = match self {
            AccessorError::DuplicateBatchTimeout
            | AccessorError::MissingDataType
            | AccessorError::EmptySelectors
            | AccessorError::MissingSelectors
            | AccessorError::InvalidSelectors(_)
            | AccessorError::InvalidLogSelector
            | AccessorError::ParseSelectors(_) => ZxStatus::INVALID_ARGS,
            AccessorError::VmoCreate(status)
            | AccessorError::VmoWrite(status)
            | AccessorError::VmoSize(status) => status,
            AccessorError::MissingFormat | AccessorError::MissingMode => ZxStatus::INVALID_ARGS,
            AccessorError::UnsupportedFormat | AccessorError::UnsupportedMode => {
                ZxStatus::WRONG_TYPE
            }
            AccessorError::Serialization { .. } => ZxStatus::BAD_STATE,
            AccessorError::Ipc { .. } | AccessorError::Io(_) => ZxStatus::IO,
        };
        control.shutdown_with_epitaph(epitaph);
    }
}
