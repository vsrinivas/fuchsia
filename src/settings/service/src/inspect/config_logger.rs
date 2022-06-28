// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::clock;
use crate::config;
use crate::inspect::utils::inspect_writable_map::InspectWritableMap;

use fuchsia_inspect::{self as inspect, component, NumericProperty, Property};
use fuchsia_inspect_derive::{Inspect, WithInspect};
use futures::lock::Mutex;
use lazy_static::lazy_static;
use std::sync::Arc;

const CONFIG_INSPECT_NODE_NAME: &str = "config_loads";

pub struct InspectConfigLogger {
    /// The inspector, stored for access in tests.
    pub inspector: &'static inspect::Inspector,

    /// The saved inspect node for the config loads.
    inspect_node: inspect::Node,

    /// The saved information about each load.
    config_load_values: InspectWritableMap<ConfigInspectInfo>,
}

/// Information about a config file load to be written to inspect.
///
/// Inspect nodes are not used, but need to be held as they're deleted from inspect once they go
/// out of scope.
#[derive(Default, Inspect)]
struct ConfigInspectInfo {
    /// Node of this info.
    inspect_node: inspect::Node,

    /// Nanoseconds since boot that this config was loaded.
    timestamp: inspect::StringProperty,

    /// Number of times the config was loaded.
    count: inspect::UintProperty,

    /// Debug string representation of the value of this config load info.
    value: inspect::StringProperty,
}

impl ConfigInspectInfo {
    fn new(timestamp: String, value: String, node: &inspect::Node, key: &str) -> Self {
        let info = Self::default()
            .with_inspect(node, key)
            .expect("Failed to create ConfigInspectInfo node");
        info.timestamp.set(&timestamp);
        info.count.set(1);
        info.value.set(&value);
        info
    }
}

lazy_static! {
    pub(crate) static ref INSPECT_CONFIG_LOGGER: Arc<Mutex<InspectConfigLogger>> =
        Arc::new(Mutex::new(InspectConfigLogger::new()));
}

impl InspectConfigLogger {
    fn new() -> Self {
        let inspector = component::inspector();
        Self {
            inspector,
            inspect_node: inspector.root().create_child(CONFIG_INSPECT_NODE_NAME),
            config_load_values: InspectWritableMap::new(),
        }
    }

    pub fn write_config_load_to_inspect(
        &mut self,
        path: String,
        config_load_info: config::base::ConfigLoadInfo,
    ) {
        let timestamp = clock::inspect_format_now();
        let config::base::ConfigLoadInfo { status, contents } = config_load_info;

        match self.config_load_values.get_mut(&path) {
            Some(config_inspect_info) => {
                config_inspect_info.timestamp.set(&timestamp);
                config_inspect_info
                    .value
                    .set(&format!("{:#?}", config::base::ConfigLoadInfo { status, contents }));
                config_inspect_info.count.add(1u64);
            }
            None => {
                let config_inspect_info = ConfigInspectInfo::new(
                    timestamp,
                    format!("{:#?}", config::base::ConfigLoadInfo { status, contents }),
                    &self.inspect_node,
                    &path,
                );
                let _ = self.config_load_values.set(path, config_inspect_info);
            }
        }
    }
}

pub struct InspectConfigLoggerHandle {
    pub logger: Arc<Mutex<InspectConfigLogger>>,
}

impl InspectConfigLoggerHandle {
    pub fn new() -> Self {
        Self { logger: INSPECT_CONFIG_LOGGER.clone() }
    }
}

impl Default for InspectConfigLoggerHandle {
    fn default() -> Self {
        InspectConfigLoggerHandle::new()
    }
}
