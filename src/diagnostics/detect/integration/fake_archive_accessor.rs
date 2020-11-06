// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod archivist_accessor;
mod archivist_server;

use {
    super::{DoneSignaler, TestData, TestEvent, TestEventSender},
    anyhow::{bail, format_err, Error},
    archivist_accessor::ArchiveAccessor,
    async_trait::async_trait,
    fidl_fuchsia_diagnostics as diagnostics,
    futures::{SinkExt, StreamExt},
    log::*,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
    test_utils_lib::injectors::ProtocolInjector,
};

/// FakeArchiveAccessor can be injected to provide test Inspect data to Detect.
pub struct FakeArchiveAccessor {
    event_sender: TestEventSender,
    done_signaler: DoneSignaler,
    inspect_data: Vec<String>,
    next_data: AtomicUsize,
}

impl FakeArchiveAccessor {
    pub fn new(
        test_data: &TestData,
        event_sender: TestEventSender,
        done_signaler: DoneSignaler,
    ) -> Arc<FakeArchiveAccessor> {
        Arc::new(FakeArchiveAccessor {
            inspect_data: test_data.inspect_data.clone(),
            event_sender,
            done_signaler,
            next_data: AtomicUsize::new(0),
        })
    }

    // Handles an Ok(request) from the FIDL channel, including all input-checking, internal
    // signaling, and data-replying. Return Ok(()) as long as nothing went wrong.
    async fn handle_fidl_request(
        &self,
        request: diagnostics::ArchiveAccessorRequest,
    ) -> Result<(), Error> {
        let mut event_sender = self.event_sender.clone();
        let diagnostics::ArchiveAccessorRequest::StreamDiagnostics {
            stream_parameters,
            result_stream,
            control_handle: _,
        } = request;
        ArchiveAccessor::validate_stream_request(stream_parameters)?;
        event_sender.send(Ok(TestEvent::DiagnosticFetch)).await.unwrap();
        let data_index = self.next_data.fetch_add(1, Ordering::Relaxed);
        if data_index >= self.inspect_data.len() {
            // We've run out of data to send. The test is done. Signal that it's time
            // to evaluate the data. Don't even respond to the Detect program.
            self.done_signaler.signal_done().await;
        } else {
            if let Err(problem) =
                ArchiveAccessor::send(result_stream, &self.inspect_data[data_index]).await
            {
                self.done_signaler.signal_done().await;
                error!("Problem in request: {}", problem);
                event_sender.send(Err(format_err!("{}", problem))).await.unwrap();
                return Err(problem);
            }
        }
        Ok(())
    }
}

#[async_trait]
impl ProtocolInjector for FakeArchiveAccessor {
    type Marker = diagnostics::ArchiveAccessorMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: diagnostics::ArchiveAccessorRequestStream,
    ) -> Result<(), Error> {
        loop {
            match request_stream.next().await {
                Some(Ok(request)) => self.handle_fidl_request(request).await?,
                Some(Err(e)) => {
                    self.done_signaler.signal_done().await;
                    bail!("{}", e);
                }
                None => break,
            }
        }
        Ok(())
    }
}
