// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::descriptor::EventDescriptor,
    futures::{future::AbortHandle, lock::Mutex},
    std::sync::Arc,
};

/// Records events from an EventStream, allowing them to be
/// flushed out into a vector at a later point in time.
pub struct EventLog {
    recorded_events: Arc<Mutex<Vec<EventDescriptor>>>,
    abort_handle: AbortHandle,
}

impl EventLog {
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
