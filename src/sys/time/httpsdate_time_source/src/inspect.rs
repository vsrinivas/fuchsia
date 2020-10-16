// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{Node, NumericProperty, UintProperty};
use httpdate_hyper::HttpsDateError;
use parking_lot::Mutex;
use std::collections::HashMap;

/// Struct containing inspect metrics for HTTPSDate.
pub struct InspectDiagnostics {
    /// Counter for samples successfully produced.
    success_count: UintProperty,
    /// Node holding failure counts.
    failure_node: Node,
    /// Counters for failed attempts to produce samples, keyed by error type.
    failure_counts: Mutex<HashMap<HttpsDateError, UintProperty>>,
}

impl InspectDiagnostics {
    /// Create a new `InspectDiagnostics` that records diagnostics to the provided root node.
    pub fn new(root_node: &Node) -> Self {
        InspectDiagnostics {
            success_count: root_node.create_uint("success_count", 0),
            failure_node: root_node.create_child("failure_counts"),
            failure_counts: Mutex::new(HashMap::new()),
        }
    }

    /// Log a successful poll to inspect.
    pub fn success(&self) {
        self.success_count.add(1);
    }

    /// Log a failed poll to inspect.
    pub fn failure(&self, error: HttpsDateError) {
        let mut failure_counts_lock = self.failure_counts.lock();
        match failure_counts_lock.get(&error) {
            Some(uint_property) => uint_property.add(1),
            None => {
                failure_counts_lock
                    .insert(error, self.failure_node.create_uint(format!("{:?}", error), 1));
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};

    #[test]
    fn test_success() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: {
                success_count: 0u64,
                failure_counts: {}
            }
        );

        inspect.success();
        inspect.success();
        assert_inspect_tree!(
            inspector,
            root: contains {
                success_count: 2u64,
            }
        );
    }

    #[test]
    fn test_failure() {
        let inspector = Inspector::new();
        let inspect = InspectDiagnostics::new(inspector.root());
        assert_inspect_tree!(
            inspector,
            root: {
                success_count: 0u64,
                failure_counts: {}
            }
        );

        inspect.failure(HttpsDateError::NoCertificatesPresented);
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {
                    NoCertificatesPresented: 1u64,
                },
            }
        );

        inspect.failure(HttpsDateError::NoCertificatesPresented);
        inspect.failure(HttpsDateError::NetworkError);
        assert_inspect_tree!(
            inspector,
            root: contains {
                failure_counts: {
                    NoCertificatesPresented: 2u64,
                    NetworkError: 1u64,
                },
            }
        );
    }
}
