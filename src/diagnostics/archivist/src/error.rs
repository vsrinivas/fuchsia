// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics::{self, BatchIteratorControlHandle};
use fuchsia_zircon_status::Status as ZxStatus;
use thiserror::Error;
use tracing::warn;

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

    #[error("couldn't parse/validate the provided selectors")]
    ParseSelectors(#[source] anyhow::Error),

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

    #[error("JSON serialization failure: {}", source)]
    Serialization {
        #[from]
        source: serde_json::Error,
    },
}

impl AccessorError {
    pub fn close(self, control: BatchIteratorControlHandle) {
        warn!(error = %self, "Closing BatchIterator.");
        let epitaph = match self {
            AccessorError::MissingDataType => ZxStatus::INVALID_ARGS,
            AccessorError::EmptySelectors
            | AccessorError::MissingSelectors
            | AccessorError::InvalidSelectors(_)
            | AccessorError::ParseSelectors(_) => ZxStatus::INVALID_ARGS,
            AccessorError::VmoCreate(status) | AccessorError::VmoWrite(status) => status,
            AccessorError::MissingFormat | AccessorError::MissingMode => ZxStatus::INVALID_ARGS,
            AccessorError::UnsupportedFormat | AccessorError::UnsupportedMode => {
                ZxStatus::WRONG_TYPE
            }
            AccessorError::Serialization { .. } => ZxStatus::BAD_STATE,
            AccessorError::Ipc { .. } => ZxStatus::IO,
        };
        control.shutdown_with_epitaph(epitaph);
    }
}
