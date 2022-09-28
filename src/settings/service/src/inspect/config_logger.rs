// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::clock;
use crate::config;
use fuchsia_inspect::{self as inspect, component, NumericProperty, Property};
use fuchsia_inspect_derive::Inspect;
use futures::lock::Mutex;
use lazy_static::lazy_static;
use settings_inspect_utils::managed_inspect_map::ManagedInspectMap;
use std::sync::Arc;

const CONFIG_INSPECT_NODE_NAME: &str = "config_loads";

pub struct InspectConfigLogger {
    /// The inspector, stored for access in tests.
    pub inspector: &'static inspect::Inspector,

    /// The saved information about each load.
    config_load_values: ManagedInspectMap<ConfigInspectInfo>,
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

    /// Counts of different results for this config's load attempts.
    result_counts: ManagedInspectMap<inspect::UintProperty>,
}

lazy_static! {
    pub(crate) static ref INSPECT_CONFIG_LOGGER: Arc<Mutex<InspectConfigLogger>> =
        Arc::new(Mutex::new(InspectConfigLogger::new()));
}

impl InspectConfigLogger {
    /// Creates a new [InspectConfigLogger] that writes to the default
    /// [fuchsia_inspect::component::inspector()].
    fn new() -> Self {
        let inspector = component::inspector();
        let config_inspect_node = inspector.root().create_child(CONFIG_INSPECT_NODE_NAME);
        Self {
            inspector,
            config_load_values: ManagedInspectMap::<ConfigInspectInfo>::with_node(
                config_inspect_node,
            ),
        }
    }

    pub fn write_config_load_to_inspect(
        &mut self,
        path: String,
        config_load_info: config::base::ConfigLoadInfo,
    ) {
        let timestamp = clock::inspect_format_now();
        let config::base::ConfigLoadInfo { status, contents } = config_load_info;
        let status_clone = status.clone();

        let config_inspect_info =
            self.config_load_values.get_or_insert_with(path, ConfigInspectInfo::default);

        config_inspect_info.timestamp.set(&timestamp);
        config_inspect_info
            .value
            .set(&format!("{:#?}", config::base::ConfigLoadInfo { status, contents }));
        config_inspect_info.count.add(1u64);
        config_inspect_info
            .result_counts
            .get_or_insert_with(status_clone.into(), inspect::UintProperty::default)
            .add(1u64);
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::base::ConfigLoadStatus;
    use fuchsia_inspect::assert_data_tree;
    use fuchsia_zircon::Time;

    #[test]
    fn test_listener_logger() {
        // Set clock for consistent timestamps.
        clock::mock::set(Time::from_nanos(0));

        let mut logger = InspectConfigLogger::new();

        logger.write_config_load_to_inspect(
            "test_path".to_string(),
            config::base::ConfigLoadInfo {
                status: ConfigLoadStatus::Success,
                contents: Some("test".to_string()),
            },
        );

        assert_data_tree!(logger.inspector, root: {
            config_loads: {
                "test_path": {
                    "count": 1u64,
                    "result_counts": {
                        "Success": 1u64,
                    },
                    "timestamp": "0.000000000",
                    "value": "ConfigLoadInfo {\n    status: Success,\n    contents: Some(\n        \"test\",\n    ),\n}"
                }
            }
        });
    }

    #[test]
    fn test_response_counts() {
        // Set clock for consistent timestamps.
        clock::mock::set(Time::from_nanos(0));

        let mut logger = InspectConfigLogger::new();

        logger.write_config_load_to_inspect(
            "test_path".to_string(),
            config::base::ConfigLoadInfo {
                status: ConfigLoadStatus::Success,
                contents: Some("test".to_string()),
            },
        );
        logger.write_config_load_to_inspect(
            "test_path".to_string(),
            config::base::ConfigLoadInfo {
                status: ConfigLoadStatus::ParseFailure("Fake parse failure".to_string()),
                contents: Some("test".to_string()),
            },
        );
        logger.write_config_load_to_inspect(
            "test_path".to_string(),
            config::base::ConfigLoadInfo {
                status: ConfigLoadStatus::ParseFailure("Fake parse failure".to_string()),
                contents: Some("test".to_string()),
            },
        );
        logger.write_config_load_to_inspect(
            "test_path".to_string(),
            config::base::ConfigLoadInfo {
                status: ConfigLoadStatus::UsingDefaults("default".to_string()),
                contents: Some("test".to_string()),
            },
        );

        assert_data_tree!(logger.inspector, root: {
            config_loads: {
                "test_path": {
                    "count": 4u64,
                    "result_counts": {
                        "Success": 1u64,
                        "ParseFailure": 2u64,
                        "UsingDefaults": 1u64,
                    },
                    "timestamp": "0.000000000",
                    "value": "ConfigLoadInfo {\n    status: UsingDefaults(\n        \"default\",\n    ),\n    contents: Some(\n        \"test\",\n    ),\n}"
                }
            }
        });
    }
}
