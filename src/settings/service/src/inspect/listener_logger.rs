// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::inspect::utils::inspect_map::InspectMap;

use fuchsia_inspect::{self as inspect, component, NumericProperty};
use fuchsia_inspect_derive::{Inspect, WithInspect};
use fuchsia_syslog::fx_log_err;

const LISTENER_INSPECT_NODE_NAME: &str = "active_listeners";

pub struct ListenerInspectLogger {
    /// The inspector, stored for access in tests.
    pub inspector: &'static inspect::Inspector,

    /// The saved inspect node for the active listeners.
    inspect_node: inspect::Node,

    /// The saved information about each setting type's active listeners.
    listener_counts: InspectMap<ListenerInspectInfo>,
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

impl ListenerInspectInfo {
    fn new(node: &inspect::Node, key: &str) -> Self {
        Self::default().with_inspect(node, key).expect("Failed to create ListenerInspectInfo node")
    }
}

impl ListenerInspectLogger {
    pub fn new() -> Self {
        let inspector = component::inspector();
        Self {
            inspector,
            inspect_node: inspector.root().create_child(LISTENER_INSPECT_NODE_NAME),
            listener_counts: InspectMap::new(),
        }
    }

    /// Adds a listener to the count for [setting_type].
    pub fn add_listener(&mut self, setting_type: SettingType) {
        let setting_type_str = format!("{:?}", setting_type);
        match self.listener_counts.get_mut(&setting_type_str) {
            Some(listener_inspect_info) => listener_inspect_info.count.add(1u64),
            None => {
                let listener_inspect_info =
                    ListenerInspectInfo::new(&self.inspect_node, &setting_type_str);
                listener_inspect_info.count.add(1u64);
                self.listener_counts.set(setting_type_str, listener_inspect_info);
            }
        }
    }

    /// Removes a listener from the count for [setting_type].
    pub fn remove_listener(&mut self, setting_type: SettingType) {
        let setting_type_str = format!("{:?}", setting_type);
        match self.listener_counts.get_mut(&setting_type_str) {
            Some(listener_inspect_info) => listener_inspect_info.count.subtract(1u64),
            None => fx_log_err!("Tried to subtract from nonexistent listener count"),
        }
    }
}
