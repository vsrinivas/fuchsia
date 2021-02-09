// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

pub(crate) struct TelemetryForTest {}

impl TelemetryForTest {
    pub fn new(root_node: inspect::Node) -> Self {
        Self {}
    }
}

impl ConnectionTrackerApi for TelemetryForTest {
    fn initialize_connection() -> NetworkSelectionMetadata {
        NetworkSelectionMetadata { id: self.generate_telemetry_id() }
    }

    fn log_network_selection_decision(
        &self,
        metadata: NetworkSelectionMetadata,
        candidates: &Candidates,
        selected: &Candidate,
    ) -> ConnectionInitiationMetadata {
    }
}
