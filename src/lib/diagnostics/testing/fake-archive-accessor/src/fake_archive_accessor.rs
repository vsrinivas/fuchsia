// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod archivist_accessor;
mod archivist_server;

use {
    anyhow::{
        bail,
        //format_err,
        Error,
    },
    archivist_accessor::ArchiveAccessor,
    async_trait::async_trait,
    fidl_fuchsia_diagnostics as diagnostics, fuchsia_async as fasync,
    futures::StreamExt,
    parking_lot::Mutex,
    std::collections::BTreeSet,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
    tracing::*,
};

/// EventSignaler supplies functions which FakeArchiveAccessor will call when events happen.
/// The integration test must implement these functions.
#[async_trait]
pub trait EventSignaler: Send + Sync {
    /// Called when Inspect data is requested and supplied over the ArchiveAccessor channel.
    async fn signal_fetch(&self);
    /// Called after all Inspect data is fetched, or an error occurs.
    async fn signal_done(&self);
    /// Called to report an error condition.
    async fn signal_error(&self, error: &str);
}

/// FakeArchiveAccessor can be injected to provide test Inspect data to programs under test.
/// On each ArchiveAccessor fetch, one of the strings in the [inspect_data] will be returned
/// to the program. Strings should be JSON-formatted Inspect data.
pub struct FakeArchiveAccessor {
    event_signaler: Option<Box<dyn EventSignaler>>,
    inspect_data: Vec<String>,
    next_data: AtomicUsize,
    // behind a mutex for interior mutability
    selectors_requested: Mutex<Vec<BTreeSet<String>>>,
}

impl FakeArchiveAccessor {
    /// Create a FakeArchiveAccessor.
    ///
    /// inspect_data: Strings to supply to the program under test via the ArchiveAccessor protocol.
    /// event_signaler: Callbacks to report events.
    pub fn new(
        inspect_data: &Vec<String>,
        event_signaler: Option<Box<dyn EventSignaler>>,
    ) -> Arc<FakeArchiveAccessor> {
        Arc::new(FakeArchiveAccessor {
            inspect_data: inspect_data.clone(),
            event_signaler,
            next_data: AtomicUsize::new(0),
            selectors_requested: Mutex::new(vec![]),
        })
    }

    // Handles an Ok(request) from the FIDL channel, including all input-checking, internal
    // signaling, and data-replying. Return Ok(()) as long as nothing went wrong.
    async fn handle_fidl_request(
        &self,
        request: diagnostics::ArchiveAccessorRequest,
    ) -> Result<(), Error> {
        let diagnostics::ArchiveAccessorRequest::StreamDiagnostics {
            stream_parameters,
            result_stream,
            control_handle: _,
        } = request;
        let selectors = ArchiveAccessor::validate_stream_request(stream_parameters)?;
        self.selectors_requested.lock().push(
            selectors
                .into_iter()
                .map(|s| selectors::selector_to_string(s))
                .collect::<Result<BTreeSet<_>, Error>>()?,
        );
        if let Some(s) = self.event_signaler.as_ref() {
            s.signal_fetch().await;
        }
        let data_index = self.next_data.fetch_add(1, Ordering::Relaxed);
        if data_index >= self.inspect_data.len() {
            // We've run out of data to send. The test is done. Signal that it's time
            // to evaluate the data. Don't even respond to the Detect program.
            if let Some(s) = self.event_signaler.as_ref() {
                s.signal_done().await;
            }
        } else {
            if let Err(problem) =
                ArchiveAccessor::send(result_stream, &self.inspect_data[data_index]).await
            {
                if let Some(s) = self.event_signaler.as_ref() {
                    s.signal_done().await;
                    s.signal_error(&format!("{}", problem)).await;
                }
                error!("Problem in request: {}", problem);
                return Err(problem);
            }
        }
        Ok(())
    }

    pub async fn serve_stream(
        &self,
        mut request_stream: diagnostics::ArchiveAccessorRequestStream,
    ) -> Result<(), Error> {
        loop {
            match request_stream.next().await {
                Some(Ok(request)) => self.handle_fidl_request(request).await?,
                Some(Err(e)) => {
                    if let Some(s) = self.event_signaler.as_ref() {
                        s.signal_done().await;
                    }
                    bail!("{}", e);
                }
                None => break,
            }
        }
        Ok(())
    }

    pub fn serve_async(self: Arc<Self>, stream: diagnostics::ArchiveAccessorRequestStream) {
        fasync::Task::spawn(async move {
            let result = self.serve_stream(stream).await;
            if let Err(e) = result {
                error!("Error while serving ArchiveAccessor: {:?}", e);
            }
        })
        .detach();
    }

    pub fn get_selectors_requested(&self) -> Vec<BTreeSet<String>> {
        self.selectors_requested.lock().clone()
    }
}
