// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This code is adapted and simplified from
// fuchsia-mirror/src/diagnostics/archivist/src/server.rs

use {
    fidl_fuchsia_diagnostics::FormattedContent,
    fidl_fuchsia_diagnostics::{
        self, BatchIteratorControlHandle, BatchIteratorRequest, BatchIteratorRequestStream,
    },
    fuchsia_zircon as zx,
    fuchsia_zircon_status::Status as ZxStatus,
    futures::prelude::*,
    log::warn,
    thiserror::Error,
};

pub struct AccessorServer {
    requests: BatchIteratorRequestStream,
}

impl AccessorServer {
    pub fn new(requests: BatchIteratorRequestStream) -> Self {
        Self { requests }
    }

    // This fails tests that try to send more data than a single VMO should hold.
    fn build_vmo(&self, data: &str) -> Result<Vec<FormattedContent>, ServerError> {
        let size = data.len() as u64;
        if size > 1024 * 1024 {
            return Err(ServerError::DataTooBig);
        }
        let vmo = zx::Vmo::create(size).map_err(ServerError::VmoCreate)?;
        vmo.write(data.as_bytes(), 0).map_err(ServerError::VmoWrite)?;
        Ok(vec![FormattedContent::Json(fidl_fuchsia_mem::Buffer { vmo, size })])
    }

    pub async fn send(mut self, data: &String) -> Result<(), ServerError> {
        if let Some(res) = self.requests.next().await {
            let BatchIteratorRequest::GetNext { responder } = res?;
            let response = self.build_vmo(data)?;
            responder.send(&mut Ok(response))?;
            if let Some(res) = self.requests.next().await {
                let BatchIteratorRequest::GetNext { responder } = res?;
                responder.send(&mut Ok(vec![]))?;
            } else {
                return Err(ServerError::TooFewBatchRequests);
            }
        } else {
            return Err(ServerError::TooFewBatchRequests);
        }
        Ok(())
    }
}

#[derive(Debug, Error)]
pub enum ServerError {
    #[error("Inspect data for test must be <1 MB")]
    DataTooBig,

    #[error("The client closed the batch connection early")]
    TooFewBatchRequests,

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

    #[error("Only Inspect supported right now")]
    UnsupportedType,

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
}

impl ServerError {
    pub fn close(self, control: BatchIteratorControlHandle) {
        warn!("Closing BatchIterator: {}", &self);
        let epitaph = match self {
            ServerError::MissingDataType | ServerError::DataTooBig => ZxStatus::INVALID_ARGS,
            ServerError::EmptySelectors
            | ServerError::MissingSelectors
            | ServerError::InvalidSelectors(_)
            | ServerError::ParseSelectors(_) => ZxStatus::INVALID_ARGS,
            ServerError::VmoCreate(status) | ServerError::VmoWrite(status) => status,
            ServerError::MissingFormat | ServerError::MissingMode => ZxStatus::INVALID_ARGS,
            ServerError::UnsupportedFormat
            | ServerError::UnsupportedMode
            | ServerError::UnsupportedType => ZxStatus::WRONG_TYPE,
            ServerError::Ipc { .. } | ServerError::TooFewBatchRequests => ZxStatus::IO,
        };
        control.shutdown_with_epitaph(epitaph);
    }
}
