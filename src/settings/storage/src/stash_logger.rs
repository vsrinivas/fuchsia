// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{self as inspect, component, NumericProperty};
use fuchsia_inspect_derive::Inspect;
use futures::lock::Mutex;
use lazy_static::lazy_static;
use settings_inspect_utils::managed_inspect_map::ManagedInspectMap;
use std::sync::Arc;

const STASH_INSPECT_NODE_NAME: &str = "stash_failures";

pub struct StashInspectLogger {
    /// The inspector, stored for access in tests.
    pub inspector: &'static inspect::Inspector,

    /// Map from a setting's device storage key to its inspect data.
    flush_failure_counts: ManagedInspectMap<StashInspectInfo>,
}

/// Contains the node and property used to record the number of stash failures for a given
/// setting.
///
/// Inspect nodes are not used, but need to be held as they're deleted from inspect once they go
/// out of scope.
#[derive(Default, Inspect)]
struct StashInspectInfo {
    /// Node of this info.
    inspect_node: inspect::Node,

    /// Number of write failures.
    count: inspect::UintProperty,
}

lazy_static! {
    // TODO(fxb/93842): replace with a dependency injected value instead of a static.
    pub(crate) static ref STASH_LOGGER: Arc<Mutex<StashInspectLogger>> =
        Arc::new(Mutex::new(StashInspectLogger::new()));
}

impl StashInspectLogger {
    fn new() -> Self {
        let inspector = component::inspector();
        let inspect_node = inspector.root().create_child(STASH_INSPECT_NODE_NAME);
        Self {
            inspector,
            flush_failure_counts: ManagedInspectMap::<StashInspectInfo>::with_node(inspect_node),
        }
    }

    /// Records a write failure for the given setting.
    pub fn record_flush_failure(&mut self, key: String) {
        let stash_inspect_info =
            self.flush_failure_counts.get_or_insert_with(key, StashInspectInfo::default);
        stash_inspect_info.count.add(1u64);
    }
}

// Handle used to access the singleton logger.
pub struct StashInspectLoggerHandle {
    pub logger: Arc<Mutex<StashInspectLogger>>,
}

impl StashInspectLoggerHandle {
    pub fn new() -> Self {
        Self { logger: Arc::clone(&STASH_LOGGER) }
    }
}

impl Default for StashInspectLoggerHandle {
    fn default() -> Self {
        StashInspectLoggerHandle::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_data_tree;

    // Verify that the StashInspectLogger accumulates failure counts to inspect.
    #[test]
    fn test_stash_logger() {
        let mut logger = StashInspectLogger::new();

        logger.record_flush_failure("test_key".to_string());
        logger.record_flush_failure("test_key2".to_string());
        logger.record_flush_failure("test_key2".to_string());

        assert_data_tree!(logger.inspector, root: {
            stash_failures: {
                "test_key": {
                    "count": 1u64,
                },
                "test_key2": {
                    "count": 2u64,
                }
            }
        });
    }
}
