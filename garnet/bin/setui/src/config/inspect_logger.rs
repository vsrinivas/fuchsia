// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::clock;
use crate::config;

use fuchsia_inspect::{self as inspect, component, NumericProperty, Property};
use futures::lock::Mutex;
use lazy_static::lazy_static;
use std::collections::HashMap;
use std::sync::Arc;

const CONFIG_INSPECT_NODE_NAME: &str = "config_loads";

pub struct InspectConfigLogger {
    /// The inspector, stored for access in tests.
    pub inspector: &'static inspect::Inspector,

    /// The saved inspect node for the config loads.
    inspect_node: inspect::Node,

    /// The saved information about each load.
    config_load_values: HashMap<String, ConfigInspectInfo>,
}

/// Information about a config file load to be written to inspect.
///
/// Inspect nodes are not used, but need to be held as they're deleted from inspect once they go
/// out of scope.
struct ConfigInspectInfo {
    /// Node of this info.
    _node: inspect::Node,

    /// Nanoseconds since boot that this config was loaded.
    timestamp: inspect::StringProperty,

    /// Number of times the config was loaded.
    count: inspect::IntProperty,

    /// Debug string representation of the value of this config load info.
    value: inspect::StringProperty,
}

lazy_static! {
    pub(crate) static ref INSPECT_CONFIG_LOGGER: Arc<Mutex<InspectConfigLogger>> =
        Arc::new(Mutex::new(InspectConfigLogger::new()));
}

impl InspectConfigLogger {
    fn new() -> Self {
        let inspector = component::inspector();
        Self {
            inspector: &inspector,
            inspect_node: inspector.root().create_child(CONFIG_INSPECT_NODE_NAME),
            config_load_values: HashMap::new(),
        }
    }

    pub fn write_config_load_to_inspect(&mut self, config_load_info: config::base::ConfigLoadInfo) {
        let timestamp = clock::inspect_format_now();
        let config::base::ConfigLoadInfo { path, status, contents } = config_load_info;
        match self.config_load_values.get_mut(&path) {
            Some(config_inspect_info) => {
                config_inspect_info.timestamp.set(&timestamp);
                config_inspect_info.value.set(&format!(
                    "{:#?}",
                    config::base::ConfigLoadInfo { path, status, contents }
                ));
                config_inspect_info.count.set(config_inspect_info.count.get().unwrap_or(0) + 1);
            }
            None => {
                // Config file not loaded before, add new entry in table.
                let node = self.inspect_node.create_child(path.clone());
                let value_prop = node.create_string(
                    "value",
                    format!(
                        "{:#?}",
                        config::base::ConfigLoadInfo { path: path.clone(), status, contents }
                    ),
                );
                let timestamp_prop = node.create_string("timestamp", timestamp.clone());
                let count_prop = node.create_int("count", 1);
                self.config_load_values.insert(
                    path,
                    ConfigInspectInfo {
                        _node: node,
                        value: value_prop,
                        timestamp: timestamp_prop,
                        count: count_prop,
                    },
                );
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
