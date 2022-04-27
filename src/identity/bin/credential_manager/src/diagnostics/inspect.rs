// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, HashTreeOperation, IncomingMethod, PinweaverMethod},
        hash_tree::HashTreeError,
    },
    fidl_fuchsia_identity_credential::CredentialError,
    fidl_fuchsia_tpm_cr50::PinWeaverError,
    fuchsia_inspect::{Inspector, Node, NumericProperty, Property, UintProperty},
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    parking_lot::Mutex,
    std::{collections::HashMap, fmt::Debug, hash::Hash},
};

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
}

/// A record in inspect of the success count and failure counts for an some operation.
struct OperationNode<E: Eq + Debug + Hash> {
    /// The inspect node used to export this information.
    _node: Node,
    /// The count of successful calls.
    success_count: UintProperty,
    /// The count of failed calls.
    error_count: UintProperty,
    /// The inpect node used to report counts of each error.
    error_node: Node,
    /// A map from (observed) errors to the count of that error.
    per_error_counts: HashMap<E, UintProperty>,
}

impl<E: Eq + Debug + Hash> OperationNode<E> {
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
    fn record(&mut self, result: Result<(), E>) {
        match result {
            Ok(()) => self.success_count.add(1),
            Err(error) => {
                let error_node = &self.error_node;
                let error_name = format!("{:?}", &error);
                self.error_count.add(1);
                self.per_error_counts
                    .entry(error)
                    .or_insert_with(|| error_node.create_uint(error_name, 0))
                    .add(1);
            }
        }
    }
}

/// A record in inspect of the success count and failure counts for incoming RPC methods.
type IncomingMethodNode = OperationNode<CredentialError>;

/// A record in inspect of the success count and failure counts for outgoing Pinweaver RPC methods.
type PinweaverMethodNode = OperationNode<PinWeaverError>;

/// A record in inspect of the success count and failure counts for hash tree operations.
type HashTreeOperationNode = OperationNode<HashTreeError>;

/// The complete set of CredentialManager information exported through Inspect.
pub struct InspectDiagnostics {
    /// Counters of success and failures for each incoming RPC.
    incoming_outcomes: Mutex<HashMap<IncomingMethod, IncomingMethodNode>>,
    /// Counters of success and failures for each outgoing Pinweaver RPC.
    pinweaver_outcomes: Mutex<HashMap<PinweaverMethod, PinweaverMethodNode>>,
    /// Counters of success and failures for each hash tree operation.
    hash_tree_operations: Mutex<HashMap<HashTreeOperation, HashTreeOperationNode>>,
    /// The number of credentials currently tracked in the hash tree.
    credential_count: UintProperty,
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
        let incoming_map = IncomingMethod::create_hash_map(|name| {
            IncomingMethodNode::new(incoming_node.create_child(name))
        });
        // Add new nodes for each outgoing Pinweaver RPC.
        let pinweaver_node = node.create_child("pinweaver");
        let pinweaver_map = PinweaverMethod::create_hash_map(|name| {
            PinweaverMethodNode::new(pinweaver_node.create_child(name))
        });
        // Add new nodes for each hash tree operation.
        let hash_tree_node = node.create_child("hash_tree");
        let hash_tree_map = HashTreeOperation::create_hash_map(|name| {
            HashTreeOperationNode::new(hash_tree_node.create_child(name))
        });
        let credential_count = node.create_uint("credential_count", 0);
        Self {
            incoming_outcomes: Mutex::new(incoming_map),
            pinweaver_outcomes: Mutex::new(pinweaver_map),
            hash_tree_operations: Mutex::new(hash_tree_map),
            credential_count,
            _nodes: vec![node.clone_weak(), incoming_node, pinweaver_node, hash_tree_node],
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

    fn pinweaver_outcome(&self, method: PinweaverMethod, result: Result<(), PinWeaverError>) {
        self.pinweaver_outcomes
            .lock()
            .get_mut(&method)
            .expect("Pinweaver RPC method missing from auto-generated map")
            .record(result);
    }

    fn hash_tree_outcome(&self, operation: HashTreeOperation, result: Result<(), HashTreeError>) {
        self.hash_tree_operations
            .lock()
            .get_mut(&operation)
            .expect("Hash tree operation missing from auto-generated map")
            .record(result);
    }

    fn credential_count(&self, count: u64) {
        self.credential_count.set(count);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::hash_tree::HashTreeError as HTE,
        fidl_fuchsia_identity_credential::CredentialError as CE,
        fidl_fuchsia_tpm_cr50::PinWeaverError as PWE,
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
                },
                pinweaver: {
                    insert_leaf: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    remove_leaf: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    reset_tree: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    try_auth: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    get_log: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    log_replay: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                },
                hash_tree: {
                    load: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    store: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                },
                credential_count: 0u64,
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
            root: contains {
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

    #[fuchsia::test]
    fn pinweaver_outcomes() {
        let inspector = Inspector::new();
        let diagnostics = InspectDiagnostics::new(&inspector.root());
        diagnostics.pinweaver_outcome(PinweaverMethod::GetLog, Ok(()));
        diagnostics.pinweaver_outcome(PinweaverMethod::TryAuth, Err(PWE::RateLimitReached));

        assert_data_tree!(
            inspector,
            root: contains {
                pinweaver: contains {
                    get_log: {
                        success_count: 1u64,
                        error_count: 0u64,
                        errors: {},
                    },
                    try_auth: {
                        success_count: 0u64,
                        error_count: 1u64,
                        errors: {
                            RateLimitReached: 1u64,
                        },
                    },
                    reset_tree: {
                        success_count: 0u64,
                        error_count: 0u64,
                        errors: {},
                    },
                }
            }
        );
    }

    #[fuchsia::test]
    fn hash_tree_outcomes() {
        let inspector = Inspector::new();
        let diagnostics = InspectDiagnostics::new(&inspector.root());
        diagnostics.hash_tree_outcome(HashTreeOperation::Load, Err(HTE::DeserializationFailed));
        diagnostics.hash_tree_outcome(HashTreeOperation::Load, Ok(()));
        diagnostics.hash_tree_outcome(HashTreeOperation::Store, Ok(()));

        assert_data_tree!(
            inspector,
            root: contains {
                hash_tree: {
                    load: {
                        success_count: 1u64,
                        error_count: 1u64,
                        errors: {
                            DeserializationFailed: 1u64,
                        },
                    },
                    store: {
                        success_count: 1u64,
                        error_count: 0u64,
                        errors: {},
                    },
                }
            }
        );
    }

    #[fuchsia::test]
    fn credential_counts() {
        let inspector = Inspector::new();
        let diagnostics = InspectDiagnostics::new(&inspector.root());
        diagnostics.credential_count(3);
        diagnostics.credential_count(1);
        diagnostics.credential_count(4);
        diagnostics.credential_count(2);

        assert_data_tree!(
            inspector,
            root: contains {
                credential_count: 2u64,
            }
        );
    }
}
