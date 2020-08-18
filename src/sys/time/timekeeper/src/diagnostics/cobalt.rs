// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use fidl_fuchsia_cobalt::CobaltEvent;
use {
    fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
};

/// A connection to the Cobalt service providing convenience functions for logging time metrics.
pub struct CobaltDiagnostics {
    /// The wrapped CobaltSender used to log metrics.
    sender: CobaltSender,
    // TODO(57677): Move back to an owned fasync::Task instead of detaching the spawned Task
    // once the lifecycle of timekeeper ensures CobaltDiagnostics objects will last long enough
    // to finish their logging.
}

impl CobaltDiagnostics {
    /// Contructs a new CobaltDiagnostics instance.
    pub fn new() -> Self {
        let (sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_id(time_metrics_registry::PROJECT_ID));
        fasync::Task::spawn(fut).detach();
        Self { sender }
    }

    #[cfg(test)]
    /// Construct a mock CobaltDiagnostics for use in unit tests, returning the CobaltDiagnostics
    /// object and an mpsc Receiver that receives any logged metrics.
    // TODO(jsankey): As design evolves consider defining CobaltDiagnostics as a trait with one
    //                implementation for production and a second mock implementation for unittest
    //                with support for ergonomic assertions.
    pub fn new_mock() -> (Self, futures::channel::mpsc::Receiver<CobaltEvent>) {
        let (mpsc_sender, mpsc_receiver) = futures::channel::mpsc::channel(1);
        let sender = CobaltSender::new(mpsc_sender);
        (CobaltDiagnostics { sender }, mpsc_receiver)
    }

    /// Records a Timekeeper lifecycle event.
    pub fn log_lifecycle_event(
        &mut self,
        event_type: time_metrics_registry::TimekeeperLifecycleEventsMetricDimensionEventType,
    ) {
        self.sender
            .log_event(time_metrics_registry::TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, event_type)
    }
}
