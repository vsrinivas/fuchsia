// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::inspect::utils::inspect_writable_map::InspectWritableMap;

use std::sync::Arc;

use fuchsia_inspect::{self as inspect, component, NumericProperty, Property};
use fuchsia_inspect_derive::{Inspect, WithInspect};
use futures::lock::Mutex;
use lazy_static::lazy_static;

const STASH_INSPECT_NODE_NAME: &str = "stash_failures";

pub struct StashInspectLogger {
    /// The inspector, stored for access in tests.
    pub inspector: &'static inspect::Inspector,

    /// The saved inspect node under which failure counts are written.
    inspect_node: inspect::Node,

    /// Map from a setting's device storage key to its inspect data.
    flush_failure_counts: InspectWritableMap<StashInspectInfo>,
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

impl StashInspectInfo {
    fn new(node: &inspect::Node, key: &str) -> Self {
        let info = Self::default()
            .with_inspect(node, key)
            .expect("Failed to create StashInspectInfo node");
        info.count.set(1);
        info
    }
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
        Self { inspector, inspect_node, flush_failure_counts: InspectWritableMap::new() }
    }

    /// Records a write failure for the given setting.
    // TODO(fxb/97284): Change to take String.
    pub fn record_flush_failure(&mut self, key: String) {
        match self.flush_failure_counts.get_mut(&key) {
            Some(stash_inspect_info) => {
                stash_inspect_info.count.add(1u64);
            }
            None => {
                let _ = self
                    .flush_failure_counts
                    .set(key.clone(), StashInspectInfo::new(&self.inspect_node, &key));
            }
        }
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
