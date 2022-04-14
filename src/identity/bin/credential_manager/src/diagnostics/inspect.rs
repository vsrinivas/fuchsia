// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, RpcMethod},
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

/// A record in inspect of the success count and failure counts for a particular RPC method.
struct RpcMethodNode {
    /// The count of successful calls.
    success_property: UintProperty,
    /// A map from (observed) errors to the count of that error.
    error_properties: HashMap<CredentialError, UintProperty>,
    /// The inspect node used to export the success and failure counters.
    node: Node,
}

impl RpcMethodNode {
    /// Create a new `RpcMethodNode` at the supplied inspect `Node`.
    fn new(node: Node) -> Self {
        let success_property = node.create_uint("Ok", 0);
        let error_properties = HashMap::new();
        Self { success_property, error_properties, node }
    }

    /// Increments the success counter if result is Ok or a failure counter if result is an Err,
    /// creating a new failure counter if this is the first time the error has been observed.
    fn increment(&mut self, result: Result<(), CredentialError>) {
        match result {
            Ok(()) => self.success_property.add(1),
            Err(error) => {
                let node = &self.node;
                self.error_properties
                    .entry(error)
                    .or_insert_with(|| node.create_uint(format!("{:?}", error), 0))
                    .add(1);
            }
        }
    }
}

/// The complete set of CredentialManager information exported through Inspect.
pub struct InspectDiagnostics {
    /// Counters of success and failures for each RPC.
    rpc_outcomes: Mutex<HashMap<RpcMethod, RpcMethodNode>>,
    /// The inspect node used to export the contents of this `InspectDiagnostics`.
    node: Node,
}

impl InspectDiagnostics {
    /// Construct a new `InspectDiagnostics` exporting at the supplied `Node`.
    pub fn new(node: &Node) -> Self {
        // Record the initialization time, mainly so we always have some content.
        node.record_int("initialization_time_nanos", zx::Time::get_monotonic().into_nanos());
        // All other nodes are created later when they are needed.
        Self { rpc_outcomes: Mutex::new(HashMap::new()), node: node.clone_weak() }
    }
}

impl Diagnostics for InspectDiagnostics {
    fn rpc_outcome(&self, method: RpcMethod, result: Result<(), CredentialError>) {
        self.rpc_outcomes
            .lock()
            .entry(method)
            .or_insert_with(|| RpcMethodNode::new(self.node.create_child(format!("{:?}", method))))
            .increment(result);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_data_tree, testing::AnyProperty},
    };

    #[fuchsia::test]
    fn after_initialization() {
        let inspector = Inspector::new();
        let _diagnostics = InspectDiagnostics::new(&inspector.root());
        assert_data_tree!(
            inspector,
            root: {
                initialization_time_nanos: AnyProperty
            }
        );
    }

    #[fuchsia::test]
    fn rpc_outcomes() {
        let inspector = Inspector::new();
        let diagnostics = InspectDiagnostics::new(&inspector.root());
        diagnostics.rpc_outcome(RpcMethod::AddCredential, Ok(()));
        diagnostics.rpc_outcome(RpcMethod::RemoveCredential, Err(CredentialError::InvalidLabel));
        diagnostics.rpc_outcome(RpcMethod::CheckCredential, Ok(()));
        diagnostics.rpc_outcome(RpcMethod::AddCredential, Err(CredentialError::NoFreeLabel));
        diagnostics.rpc_outcome(RpcMethod::AddCredential, Err(CredentialError::InternalError));
        diagnostics.rpc_outcome(RpcMethod::RemoveCredential, Err(CredentialError::InvalidLabel));

        assert_data_tree!(
            inspector,
            root: {
                initialization_time_nanos: AnyProperty,
                AddCredential: {
                    Ok: 1u64,
                    NoFreeLabel: 1u64,
                    InternalError: 1u64,
                },
                CheckCredential: {
                    Ok: 1u64,
                },
                RemoveCredential: {
                    Ok: 0u64,
                    InvalidLabel: 2u64,
                },
            }
        );
    }
}
