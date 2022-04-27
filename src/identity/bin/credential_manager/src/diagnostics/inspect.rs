// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, HashTreeOperation, IncomingMethod},
        hash_tree::HashTreeError,
    },
    fidl_fuchsia_identity_credential::CredentialError,
    fuchsia_inspect::{Inspector, Node, NumericProperty, UintProperty},
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    parking_lot::Mutex,
    std::collections::HashMap,
};

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
}

/// A record in inspect of the success count and failure counts for an incoming RPC method.
struct IncomingMethodNode {
    /// The inspect node used to export this information.
    _node: Node,
    /// The count of successful calls.
    success_count: UintProperty,
    /// The count of failed calls.
    error_count: UintProperty,
    /// The inpect node used to report counts of each error.
    error_node: Node,
    /// A map from (observed) errors to the count of that error.
    per_error_counts: HashMap<CredentialError, UintProperty>,
}

impl IncomingMethodNode {
    /// Create a new `IncomingMethodNode` at the supplied inspect `Node`.
    fn new(node: Node) -> Self {
        Self {
            success_count: node.create_uint("success_count", 0),
            error_count: node.create_uint("error_count", 0),
            error_node: node.create_child("errors"),
            per_error_counts: HashMap::new(),
            _node: node,
        }
    }

    /// Increments either the success or the error counters, creating a new error counter if
    /// this is the first time the error has been observed.
    fn record(&mut self, result: Result<(), CredentialError>) {
        match result {
            Ok(()) => self.success_count.add(1),
            Err(error) => {
                let error_node = &self.error_node;
                self.error_count.add(1);
                self.per_error_counts
                    .entry(error)
                    .or_insert_with(|| error_node.create_uint(format!("{:?}", error), 0))
                    .add(1);
            }
        }
    }
}

/// The complete set of CredentialManager information exported through Inspect.
pub struct InspectDiagnostics {
    /// Counters of success and failures for each incoming RPC.
    incoming_outcomes: Mutex<HashMap<IncomingMethod, IncomingMethodNode>>,
    /// A vector of inspect nodes that must be kept in scope for the lifetime of this object.
    _nodes: Vec<Node>,
}

impl InspectDiagnostics {
    /// Construct a new `InspectDiagnostics` exporting at the supplied `Node`.
    pub fn new(node: &Node) -> Self {
        // Record the initialization time.
        node.record_int("initialization_time_nanos", zx::Time::get_monotonic().into_nanos());
        // Add new nodes for each incoming RPC.
        let incoming_node = node.create_child("incoming");
        let mut incoming_map = HashMap::new();
        for (method, name) in IncomingMethod::name_map().iter() {
            incoming_map
                .insert(*method, IncomingMethodNode::new(incoming_node.create_child(*name)));
        }
        Self {
            incoming_outcomes: Mutex::new(incoming_map),
            _nodes: vec![node.clone_weak(), incoming_node],
        }
    }
}

impl Diagnostics for InspectDiagnostics {
    fn incoming_outcome(&self, method: IncomingMethod, result: Result<(), CredentialError>) {
        self.incoming_outcomes
            .lock()
            .get_mut(&method)
            .expect("Incoming RPC method missing from auto-generated map")
            .record(result);
    }

    fn hash_tree_outcome(&self, _operation: HashTreeOperation, _result: Result<(), HashTreeError>) {
        //TODO(fxb/91714): Record HashTree metrics in inspect.
    }

    fn credential_count(&self, _count: u64) {
        //TODO(fxb/91714): Record credential count metrics in inspect.
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_identity_credential::CredentialError as CE,
        fuchsia_inspect::{assert_data_tree, testing::AnyProperty},
    };

    #[fuchsia::test]
    fn after_initialization() {
        let inspector = Inspector::new();
        let _diagnostics = InspectDiagnostics::new(&inspector.root());
        assert_data_tree!(
            inspector,
            root: {
                initialization_time_nanos: AnyProperty,
                incoming: {
                    add_credential: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    check_credential: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    remove_credential: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                }
            }
        );
    }

    #[fuchsia::test]
    fn incoming_outcomes() {
        let inspector = Inspector::new();
        let diagnostics = InspectDiagnostics::new(&inspector.root());
        diagnostics.incoming_outcome(IncomingMethod::AddCredential, Ok(()));
        diagnostics.incoming_outcome(IncomingMethod::RemoveCredential, Err(CE::InvalidLabel));
        diagnostics.incoming_outcome(IncomingMethod::CheckCredential, Ok(()));
        diagnostics.incoming_outcome(IncomingMethod::AddCredential, Err(CE::NoFreeLabel));
        diagnostics.incoming_outcome(IncomingMethod::AddCredential, Err(CE::InternalError));
        diagnostics.incoming_outcome(IncomingMethod::RemoveCredential, Err(CE::InvalidLabel));

        assert_data_tree!(
            inspector,
            root: {
                initialization_time_nanos: AnyProperty,
                incoming: {
                    add_credential: {
                        success_count: 1u64,
                        error_count: 2u64,
                        errors: {
                            NoFreeLabel: 1u64,
                            InternalError: 1u64,
                        },
                    },
                    check_credential: {
                        success_count: 1u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    remove_credential: {
                        success_count: 0u64,
                        error_count: 2u64,
                        errors: {
                            InvalidLabel: 2u64,
                        },
                    },
                }
            }
        );
    }
}
