// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::DiagnosticsServerStats,
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_diagnostics::{self, BatchIteratorMarker, BatchIteratorRequestStream},
    fuchsia_async::{self as fasync},
    fuchsia_inspect::NumericProperty,
    futures::stream::FusedStream,
    futures::{TryFutureExt, TryStreamExt},
    log::{error, warn},
    std::sync::Arc,
};

#[async_trait]
pub trait DiagnosticsServer: 'static + Sized + Send + Sync {
    /// Serve a snapshot of the buffered diagnostics data on system.
    async fn serve_snapshot(
        &self,
        stream: &mut BatchIteratorRequestStream,
        format: &fidl_fuchsia_diagnostics::Format,
        diagnostics_server_stats: Arc<DiagnosticsServerStats>,
    ) -> Result<(), Error>;

    /// Serve an empty vector to the client. The terminal vector will be sent
    /// until the client closes their connection.
    async fn serve_terminal_batch(
        &self,
        stream: &mut BatchIteratorRequestStream,
        diagnostics_server_stats: Arc<DiagnosticsServerStats>,
    ) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }
        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    diagnostics_server_stats.global_stats.batch_iterator_get_next_requests.add(1);
                    diagnostics_server_stats.global_stats.batch_iterator_get_next_responses.add(1);
                    diagnostics_server_stats.batch_iterator_get_next_requests.add(1);
                    diagnostics_server_stats.batch_iterator_get_next_responses.add(1);
                    diagnostics_server_stats.batch_iterator_terminal_responses.add(1);
                    responder.send(&mut Ok(Vec::new()))?;
                }
            }
        }
        Ok(())
    }

    fn stream_diagnostics(
        self,
        stream_mode: fidl_fuchsia_diagnostics::StreamMode,
        format: fidl_fuchsia_diagnostics::Format,
        result_stream: ServerEnd<BatchIteratorMarker>,
        server_stats: Arc<DiagnosticsServerStats>,
    ) -> Result<(), Error> {
        let result_channel = fasync::Channel::from_channel(result_stream.into_channel())?;
        let errorful_server_stats = server_stats.clone();

        fasync::Task::spawn(
            async move {
                server_stats.global_stats.batch_iterator_connections_opened.add(1);

                let mut iterator_req_stream =
                    fidl_fuchsia_diagnostics::BatchIteratorRequestStream::from_channel(
                        result_channel,
                    );

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Snapshot
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    self.serve_snapshot(&mut iterator_req_stream, &format, server_stats.clone())
                        .await?;
                }

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Subscribe
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    error!("not yet supported");
                }
                self.serve_terminal_batch(&mut iterator_req_stream, server_stats.clone()).await?;
                server_stats.global_stats.batch_iterator_connections_closed.add(1);
                Ok(())
            }
            .unwrap_or_else(move |e: anyhow::Error| {
                errorful_server_stats.global_stats.batch_iterator_get_next_errors.add(1);
                errorful_server_stats.global_stats.batch_iterator_connections_closed.add(1);
                warn!("Error encountered running diagnostics server: {:?}", e);
            }),
        )
        .detach();
        Ok(())
    }
}
