// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::FORMATTED_CONTENT_CHUNK_SIZE_TARGET,
        diagnostics::DiagnosticsServerStats,
        formatter::{FormattedContentBatcher, JsonPacketSerializer, JsonString},
    },
    diagnostics_data::{Data, DiagnosticsData},
    fidl_fuchsia_diagnostics::{
        self, BatchIteratorControlHandle, BatchIteratorRequest, BatchIteratorRequestStream,
        FormattedContent,
    },
    fuchsia_zircon_status::Status as ZxStatus,
    futures::prelude::*,
    log::warn,
    serde::Serialize,
    std::{ops::Deref, pin::Pin, sync::Arc},
    thiserror::Error,
};

pub struct AccessorServer {
    requests: BatchIteratorRequestStream,
    stats: Arc<DiagnosticsServerStats>,
    data: FormattedStream,
}

type FormattedStream =
    Pin<Box<dyn Stream<Item = Vec<Result<FormattedContent, ServerError>>> + Send>>;

impl AccessorServer {
    pub fn new<Items, D>(
        data: Items,
        requests: BatchIteratorRequestStream,
        stats: Arc<DiagnosticsServerStats>,
    ) -> Result<Self, ServerError>
    where
        Items: Stream<Item = Data<D>> + Send + 'static,
        D: DiagnosticsData,
    {
        let result_stats = stats.clone();
        let data = data.map(move |d| {
            if D::has_errors(&d.metadata) {
                result_stats.add_result_error();
            }
            let res = JsonString::serialize(&d);
            if res.is_err() {
                result_stats.add_result_error();
            }
            result_stats.add_result();
            res
        });

        let data = Box::pin(FormattedContentBatcher::new(data, stats.clone()));
        Self::new_inner(data, requests, stats)
    }

    pub fn new_serving_arrays<D, P, S>(
        data: S,
        requests: BatchIteratorRequestStream,
        stats: Arc<DiagnosticsServerStats>,
    ) -> Result<Self, ServerError>
    where
        P: Deref<Target = D>,
        D: Serialize,
        S: Stream<Item = P> + Send + Unpin + 'static,
    {
        let serialized =
            JsonPacketSerializer::new(stats.clone(), FORMATTED_CONTENT_CHUNK_SIZE_TARGET, data);
        let data = Box::pin(FormattedContentBatcher::new(serialized, stats.clone()));

        Self::new_inner(data, requests, stats)
    }

    fn new_inner(
        data: FormattedStream,
        requests: BatchIteratorRequestStream,
        stats: Arc<DiagnosticsServerStats>,
    ) -> Result<Self, ServerError> {
        stats.open_connection();
        Ok(Self { data, requests, stats })
    }

    pub async fn run(mut self) -> Result<(), ServerError> {
        while let Some(res) = self.requests.next().await {
            let BatchIteratorRequest::GetNext { responder } = res?;
            self.stats.add_request();
            // if we get None back, treat that as a terminal batch with an empty vec
            let batch = self.data.next().await.unwrap_or(vec![]);
            // turn errors into epitaphs -- we drop intermediate items if there was an error midway
            let batch = batch.into_iter().collect::<Result<Vec<_>, _>>()?;

            // increment counters
            self.stats.add_response();
            if batch.is_empty() {
                self.stats.add_terminal();
            }

            let mut response = Ok(batch);
            responder.send(&mut response)?;
        }
        Ok(())
    }
}

impl Drop for AccessorServer {
    fn drop(&mut self) {
        self.stats.close_connection();
    }
}

#[derive(Debug, Error)]
pub enum ServerError {
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

impl ServerError {
    pub fn close(self, control: BatchIteratorControlHandle) {
        warn!("Closing BatchIterator: {}", &self);
        let epitaph = match self {
            ServerError::MissingDataType => ZxStatus::INVALID_ARGS,
            ServerError::EmptySelectors
            | ServerError::MissingSelectors
            | ServerError::InvalidSelectors(_)
            | ServerError::ParseSelectors(_) => ZxStatus::INVALID_ARGS,
            ServerError::VmoCreate(status) | ServerError::VmoWrite(status) => status,
            ServerError::MissingFormat | ServerError::MissingMode => ZxStatus::INVALID_ARGS,
            ServerError::UnsupportedFormat | ServerError::UnsupportedMode => ZxStatus::WRONG_TYPE,
            ServerError::Serialization { .. } => ZxStatus::BAD_STATE,
            ServerError::Ipc { .. } => ZxStatus::IO,
        };
        control.shutdown_with_epitaph(epitaph);
    }
}
