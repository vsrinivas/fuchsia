// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use fuchsia_inspect::{self as inspect, component, NumericProperty};
use fuchsia_inspect_derive::Inspect;
use fuchsia_syslog::fx_log_err;
use settings_inspect_utils::managed_inspect_map::ManagedInspectMap;

const LISTENER_INSPECT_NODE_NAME: &str = "active_listeners";

pub struct ListenerInspectLogger {
    /// The saved information about each setting type's active listeners.
    listener_counts: ManagedInspectMap<ListenerInspectInfo>,
}

impl Default for ListenerInspectLogger {
    fn default() -> Self {
        Self::new()
    }
}

/// Information about active listeners to be written to inspect.
///
/// Inspect nodes are not used, but need to be held as they're deleted from inspect once they go
/// out of scope.
#[derive(Default, Inspect)]
struct ListenerInspectInfo {
    /// Node of this info.
    inspect_node: inspect::Node,

    /// Number of active listeners.
    count: inspect::UintProperty,
}

impl ListenerInspectLogger {
    /// Creates a new [ListenerInspectLogger] that writes to the default
    /// [fuchsia_inspect::component::inspector()].
    pub fn new() -> Self {
        Self::with_inspector(component::inspector())
    }

    pub fn with_inspector(inspector: &inspect::Inspector) -> Self {
        let listener_counts_node = inspector.root().create_child(LISTENER_INSPECT_NODE_NAME);
        Self {
            listener_counts: ManagedInspectMap::<ListenerInspectInfo>::with_node(
                listener_counts_node,
            ),
        }
    }

    /// Adds a listener to the count for [setting_type].
    pub fn add_listener(&mut self, setting_type: SettingType) {
        let setting_type_str = format!("{:?}", setting_type);
        let inspect_info =
            self.listener_counts.get_or_insert_with(setting_type_str, ListenerInspectInfo::default);
        inspect_info.count.add(1u64);
    }

    /// Removes a listener from the count for [setting_type].
    pub fn remove_listener(&mut self, setting_type: SettingType) {
        let setting_type_str = format!("{:?}", setting_type);
        match self.listener_counts.map_mut().get_mut(&setting_type_str) {
            Some(listener_inspect_info) => listener_inspect_info.count.subtract(1u64),
            None => fx_log_err!("Tried to subtract from nonexistent listener count"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ListenerInspectLogger;
    use fuchsia_inspect::assert_data_tree;

    #[test]
    fn test_listener_logger() {
        let inspector = inspect::Inspector::new();

        let mut logger = ListenerInspectLogger::with_inspector(&inspector);

        logger.add_listener(SettingType::Unknown);
        logger.add_listener(SettingType::Unknown);
        logger.add_listener(SettingType::Unknown);

        logger.remove_listener(SettingType::Unknown);

        // Since listeners were added thrice and removed once, the count at the end is 2.
        assert_data_tree!(inspector, root: {
            active_listeners: {
                Unknown: {
                    "count": 2u64,
                }
            }
        });
    }
}
