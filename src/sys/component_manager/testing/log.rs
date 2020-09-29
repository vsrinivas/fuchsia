// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        descriptor::EventDescriptor,
        events::{EventSource, EventStream},
    },
    anyhow::Error,
    fuchsia_async as fasync,
    futures::{
        future::{AbortHandle, Abortable, TryFutureExt},
        lock::Mutex,
    },
    std::{convert::TryFrom, sync::Arc},
};

/// Records events from an EventStream, allowing them to be
/// flushed out into a vector at a later point in time.
pub struct EventLog {
    recorded_events: Arc<Mutex<Vec<EventDescriptor>>>,
    abort_handle: AbortHandle,
}

impl EventLog {
    /// Subscribe to the provided `event_names`, and log the `EventDescriptors`
    /// in a separate task.
    pub async fn record_events(
        event_source: &mut EventSource,
        event_names: Vec<impl AsRef<str>>,
    ) -> Result<EventLog, Error> {
        let event_stream = event_source.subscribe(event_names).await?;
        Ok(EventLog::new(event_stream))
    }

    fn new(mut event_stream: EventStream) -> Self {
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let recorded_events = Arc::new(Mutex::new(vec![]));
        {
            // Start an async task that records events from the event_stream
            let recorded_events = recorded_events.clone();
            fasync::Task::spawn(
                Abortable::new(
                    async move {
                        loop {
                            // Get the next event from the event_stream
                            let event = event_stream
                                .next()
                                .await
                                .expect("Failed to get next event from EventStreamSync");

                            // Construct the EventDescriptor from the Event
                            let recorded_event = EventDescriptor::try_from(&event)
                                .expect("Failed to convert Event to EventDescriptor");

                            // Insert the event into the list
                            {
                                let mut recorded_events = recorded_events.lock().await;
                                recorded_events.push(recorded_event);
                            }
                        }
                    },
                    abort_registration,
                )
                .unwrap_or_else(|_| ()),
            )
            .detach();
        }
        Self { recorded_events, abort_handle }
    }

    pub async fn flush(&self) -> Vec<EventDescriptor> {
        // Lock and flush out all events from the vector
        let mut recorded_events = self.recorded_events.lock().await;
        recorded_events.drain(..).collect()
    }
}

impl Drop for EventLog {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}
