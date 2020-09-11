// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use crate::{
    constants::MAXIMUM_FORMATTED_LOGS_CONTENT_SIZE, diagnostics::DiagnosticsServerStats,
    formatter::ChunkedJsonArraySerializer, logs::LogManager, server::DiagnosticsServer,
};
use anyhow::{ensure, Error};
use fidl_fuchsia_diagnostics::{BatchIteratorRequest, BatchIteratorRequestStream, Format};
use futures::prelude::*;
use std::sync::Arc;

/// An individual connection streaming logs through ArchiveAccessor.
pub struct LogServer {
    logs: LogManager,
    stats: Arc<DiagnosticsServerStats>,
}

impl LogServer {
    // TODO(fxbug.dev/59620) implement selectors
    pub fn new(
        logs: LogManager,
        format: Format,
        stats: Arc<DiagnosticsServerStats>,
    ) -> Result<Self, Error> {
        ensure!(matches!(format, Format::Json), "only json supported right now");
        Ok(Self { logs, stats })
    }
}

#[async_trait::async_trait]
impl DiagnosticsServer for LogServer {
    fn stats(&self) -> &Arc<DiagnosticsServerStats> {
        &self.stats
    }

    fn format(&self) -> &Format {
        &Format::Json
    }

    async fn snapshot(&self, stream: &mut BatchIteratorRequestStream) -> Result<(), Error> {
        let mut messages = self.logs.inner.lock().await.log_msg_buffer.collect().await;
        messages.sort_by_key(|m| m.metadata.timestamp);
        let mut serialized = ChunkedJsonArraySerializer::new(
            self.stats.clone(),
            MAXIMUM_FORMATTED_LOGS_CONTENT_SIZE,
            messages.iter().map(|m| &**m),
        );

        while let Some(res) = stream.next().await {
            let BatchIteratorRequest::GetNext { responder } = res?;
            self.stats().add_request();
            let mut result = Ok(serialized.next_batch()?);
            self.stats().add_response();
            responder.send(&mut result)?;
        }
        Ok(())
    }
}
