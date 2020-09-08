// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::DiagnosticsServerStats,
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_diagnostics::{
        self, BatchIteratorMarker, BatchIteratorRequestStream, Format, StreamMode,
    },
    fuchsia_async::{self as fasync, Task},
    futures::stream::FusedStream,
    futures::TryStreamExt,
    log::warn,
    std::sync::Arc,
};

#[async_trait]
pub trait DiagnosticsServer: 'static + Sized + Send + Sync {
    /// Return a reference to the stats node for this server.
    fn stats(&self) -> &Arc<DiagnosticsServerStats>;

    /// Return the format which was requested by the client.
    fn format(&self) -> &Format;

    /// Serve a snapshot of the buffered diagnostics data.
    async fn snapshot(&self, stream: &mut BatchIteratorRequestStream) -> Result<(), Error>;

    /// Serve an ongoing subscription of the buffered diagnostics data.
    async fn subscribe(&self, _stream: &mut BatchIteratorRequestStream) -> Result<(), Error> {
        warn!("subscriptions not yet supported");
        Ok(())
    }

    /// Serve an empty vector to the client. The terminal vector will be sent
    /// until the client closes their connection.
    async fn terminate_batch(&self, stream: &mut BatchIteratorRequestStream) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }
        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    self.stats().add_terminal();
                    responder.send(&mut Ok(Vec::new()))?;
                }
            }
        }
        Ok(())
    }

    async fn serve(
        self,
        stream_mode: StreamMode,
        result_stream: ServerEnd<BatchIteratorMarker>,
    ) -> Result<(), Error> {
        let result_channel = fasync::Channel::from_channel(result_stream.into_channel())?;
        let mut requests = BatchIteratorRequestStream::from_channel(result_channel);

        if matches!(stream_mode, StreamMode::Snapshot | StreamMode::SnapshotThenSubscribe) {
            self.snapshot(&mut requests).await?;
        }

        if matches!(stream_mode, StreamMode::SnapshotThenSubscribe | StreamMode::Subscribe) {
            self.subscribe(&mut requests).await?;
        }

        self.terminate_batch(&mut requests).await?;
        Ok(())
    }

    /// Spawn a `Task` to serve the request.
    fn spawn(
        self,
        stream_mode: StreamMode,
        result_stream: ServerEnd<BatchIteratorMarker>,
    ) -> Task<()> {
        Task::spawn(async move {
            let stats = self.stats().clone();
            stats.open_connection();
            if let Err(e) = self.serve(stream_mode, result_stream).await {
                stats.add_error();
                warn!("Error encountered running diagnostics server: {:?}", e);
            }
            stats.close_connection();
        })
    }
}
