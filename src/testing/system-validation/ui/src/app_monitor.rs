// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync,
    std::sync::{Arc, Mutex},
};

// Helper for getting information about the child component using event_stream.
pub struct AppMonitor {
    moniker: String,
}

impl AppMonitor {
    pub fn new(moniker: String) -> Self {
        Self { moniker: moniker }
    }

    // Non-blocking. Starts a separate process that waits for a Stopped event from the moniker.
    // Note: this is a best effort check, the stopped event is only being observed while the
    // system validation test is running.
    pub fn monitor_for_stop_event(&self, stopped: &Arc<Mutex<bool>>) {
        *stopped.lock().unwrap() = false;
        let stopped_clone = stopped.clone();
        let moniker_clone = self.moniker.clone();
        fasync::Task::spawn(async move {
            let mut event_stream = EventStream::open().await.unwrap();
            EventMatcher::ok()
                .moniker(&moniker_clone)
                .wait::<Stopped>(&mut event_stream)
                .await
                .expect(format!("failed to observe {} stop event", &moniker_clone).as_str());
            *stopped_clone.lock().unwrap() = true;
        })
        .detach();
    }

    // Blocking. Waits for event matcher to report that app is running.
    pub async fn wait_for_start_event(&self) {
        let mut event_stream = EventStream::open().await.unwrap();
        let moniker = self.moniker.clone();

        EventMatcher::ok()
            .moniker(&moniker)
            .wait::<Started>(&mut event_stream)
            .await
            .expect(format!("failed to observe {} start event", &moniker).as_str());
    }
}
