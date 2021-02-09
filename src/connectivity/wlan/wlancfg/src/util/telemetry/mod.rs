// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::types,
    fuchsia_inspect::{self as inspect},
    fuchsia_inspect_contrib::{
        inspect_insert, inspect_log,
        log::{InspectListClosure, WriteInspect},
        make_inspect_loggable,
        nodes::BoundedListNode,
        nodes::NodeWriter,
    },
    rand::Rng as _,
    std::collections::HashMap,
    std::sync::{Arc, Mutex, RwLock},
};

const MAX_PAST_NETWORK_SELECTIONS: usize = 5;
const MAX_PAST_CONNECTIONS: usize = 5;

struct InspectNodes {
    network_selections: BoundedListNode,
    active_connections: inspect::Node,
    past_connections: BoundedListNode,
}

#[derive(Clone)]
pub(crate) struct Telemetry {
    inspect_nodes: Arc<Mutex<InspectNodes>>,
    active_connections: Arc<RwLock<Vec<()>>>,
    past_connections: Arc<RwLock<Vec<()>>>,
}

impl Telemetry {
    pub fn new(root_node: inspect::Node) -> Self {
        Self {
            inspect_nodes: Arc::new(Mutex::new(InspectNodes {
                network_selections: BoundedListNode::new(
                    root_node.create_child("network_selections"),
                    MAX_PAST_NETWORK_SELECTIONS,
                ),
                active_connections: root_node.create_child("active_connections"),
                past_connections: BoundedListNode::new(
                    root_node.create_child("past_connections"),
                    MAX_PAST_CONNECTIONS,
                ),
            })),
            active_connections: Arc::new(RwLock::new(vec![])),
            past_connections: Arc::new(RwLock::new(vec![])),
        }
    }
    fn generate_telemetry_id(&self) -> TelemetryId {
        rand::thread_rng().gen()
    }
}
pub(crate) trait ConnectionTrackerApi {
    fn initialize_connection(&self) -> NetworkSelectionMetadata;
    fn log_network_selection_decision(
        &self,
        metadata: NetworkSelectionMetadata,
        search_type: NetworkSelectionType,
        candidates: Vec<CandidateBss>,
        selected: CandidateBss,
    ) -> ConnectionInitiationMetadata;
    // fn log_connection_attempt_initiation(&self, metadata: &mut ConnectionMetadata);
    // fn log_connection_attempt_result(&self, metadata: &mut ConnectionMetadata, result: Result);
    // fn log_disconnection(&self, metadata: &mut ConnectionMetadata, reason: Reason);
    // fn log_connection_completion(&self, metadata: ConnectionMetadata);
}

type TelemetryId = u32;

pub(crate) struct NetworkSelectionMetadata {
    id: TelemetryId,
}

pub(crate) struct ConnectionInitiationMetadata {}

pub(crate) struct CandidateBss {
    rssi: i8,
    recent_failure_count: u8,
    band: types::Band,
    score: i8,
}
impl WriteInspect for CandidateBss {
    fn write_inspect(&self, writer: &mut NodeWriter<'_>, key: &str) {
        inspect_insert!(writer, var key: {
            rssi: self.rssi,
            recent_failure_count: self.recent_failure_count,
            band: format!("{:?}", self.band),
            score: self.score,
        });
    }
}

pub(crate) enum NetworkSelectionType {
    Undirected, // Looking for the best BSS from any saved networks
    Directed,   // Looking for the best BSS for a particular network
}
impl WriteInspect for NetworkSelectionType {
    fn write_inspect(&self, writer: &mut NodeWriter<'_>, key: &str) {
        writer.create_string(
            key,
            match self {
                NetworkSelectionType::Undirected => "Undirected",
                NetworkSelectionType::Directed => "Directed",
            },
        );
    }
}

impl ConnectionTrackerApi for Telemetry {
    fn initialize_connection(&self) -> NetworkSelectionMetadata {
        NetworkSelectionMetadata { id: self.generate_telemetry_id() }
    }

    fn log_network_selection_decision(
        &self,
        metadata: NetworkSelectionMetadata,
        search_type: NetworkSelectionType,
        candidates: Vec<CandidateBss>,
        selected: CandidateBss,
    ) -> ConnectionInitiationMetadata {
        let candidates_mapped =
            InspectListClosure(&candidates, |mut node_writer, key, candidate| {
                inspect_insert!(node_writer, var key: candidate);
            });
        inspect_log!(self.inspect_nodes.lock().unwrap().network_selections, {
            search_type: search_type,
            selected: selected,
            candidates: candidates_mapped,
        });
        ConnectionInitiationMetadata {}
    }
    // fn log_connection_attempt_initiation(metadata: &mut ConnectionMetadata);
    // fn log_connection_attempt_result(metadata: &mut ConnectionMetadata, result: Result);
    // fn log_disconnection(metadata: &mut ConnectionMetadata, reason: Reason);
    // fn log_connection_completion(metadata: ConnectionMetadata);
}

// BRAINSTORM
// I'd like the Telemetry module to be able to track uptime, expressed as (time connected)  / (time the device was on and had a saved network in the vicinity)
// For this, I would need an event log for "connected" vs "not connected", along with an event log
// for "saved networks seen in area" vs "saved networks not seen in area".
// For these metrics, the Telemetry module can trivially operate as a MPSC. For

#[cfg(test)]
mod tests {

    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty, Inspector},
        wlan_common::assert_variant,
    };

    #[test]
    fn test_log_network_selection_decision() {
        let inspector = Inspector::new();
        let test_node = inspector.root().create_child("test_node");
        test_node.create_uint("foo", 1);
        let telemetry = Telemetry::new(test_node);

        telemetry.log_network_selection_decision(
            NetworkSelectionMetadata { id: 20 },
            NetworkSelectionType::Undirected,
            vec![],
            CandidateBss {
                rssi: 20,
                recent_failure_count: 10,
                band: types::Band::WlanBand2Ghz,
                score: 25,
            },
        );

        assert_inspect_tree!(inspector, root: {
            test_node: {
                foo: 1u64
            }
        });

        assert_inspect_tree!(inspector, root: {
            test_node: {
                "0": { "@time": AnyProperty, k1: "1", meaning_of_life: 42u64, k3: 3i64, k4: 4f64 },
                "1": { "@time": AnyProperty, small_uint: 1u64, small_int: 2i64, float: 3f64 },
                "2": { "@time": AnyProperty, s: "str", uint: 13u64 },
                "3": { "@time": AnyProperty },
            }
        });
    }
}
