// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_update::State;
use fuchsia_inspect::{Node, Property, StringProperty};
use omaha_client::{
    common::App,
    configuration::{Config, Updater},
    protocol::request::OS,
};

pub struct ConfigurationNode {
    _node: Node,
    updater: UpdaterNode,
    os: OsNode,
    omaha: OmahaNode,
}

impl ConfigurationNode {
    pub fn new(configuration_node: Node) -> Self {
        ConfigurationNode {
            updater: UpdaterNode::new(configuration_node.create_child("updater")),
            os: OsNode::new(configuration_node.create_child("os")),
            omaha: OmahaNode::new(configuration_node.create_child("omaha")),
            _node: configuration_node,
        }
    }

    pub fn set(&self, config: &Config) {
        self.updater.set(&config.updater);
        self.os.set(&config.os);
        self.omaha.set(&config.service_url);
    }
}

struct UpdaterNode {
    _node: Node,
    name: StringProperty,
    version: StringProperty,
}

impl UpdaterNode {
    fn new(updater_node: Node) -> Self {
        UpdaterNode {
            name: updater_node.create_string("name", ""),
            version: updater_node.create_string("version", ""),
            _node: updater_node,
        }
    }

    fn set(&self, updater: &Updater) {
        self.name.set(&updater.name);
        self.version.set(&updater.version.to_string());
    }
}

struct OsNode {
    _node: Node,
    platform: StringProperty,
    version: StringProperty,
    service_pack: StringProperty,
    arch: StringProperty,
}

impl OsNode {
    fn new(os_node: Node) -> Self {
        OsNode {
            platform: os_node.create_string("platform", ""),
            version: os_node.create_string("version", ""),
            service_pack: os_node.create_string("service_pack", ""),
            arch: os_node.create_string("arch", ""),
            _node: os_node,
        }
    }

    fn set(&self, os: &OS) {
        self.platform.set(&os.platform);
        self.version.set(&os.version);
        self.service_pack.set(&os.service_pack);
        self.arch.set(&os.arch);
    }
}

struct OmahaNode {
    _node: Node,
    service_url: StringProperty,
}

impl OmahaNode {
    fn new(omaha_node: Node) -> Self {
        OmahaNode { service_url: omaha_node.create_string("service_url", ""), _node: omaha_node }
    }

    fn set(&self, service_url: &str) {
        self.service_url.set(service_url);
    }
}

pub struct AppsNode {
    _node: Node,
    apps: StringProperty,
}

impl AppsNode {
    pub fn new(apps_node: Node) -> Self {
        AppsNode { apps: apps_node.create_string("apps", ""), _node: apps_node }
    }

    pub fn set(&self, apps: &[App]) {
        self.apps.set(&format!("{:?}", apps));
    }
}

pub struct StateNode {
    _node: Node,
    state: StringProperty,
}

impl StateNode {
    pub fn new(state_node: Node) -> Self {
        StateNode { state: state_node.create_string("state", ""), _node: state_node }
    }

    pub fn set(&self, state: &State) {
        self.state.set(&format!("{:?}", state));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::configuration::get_config;
    use fidl_fuchsia_update::ManagerState;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use omaha_client::protocol::Cohort;

    #[test]
    fn test_configuration_node() {
        let inspector = Inspector::new();
        let node = ConfigurationNode::new(inspector.root().create_child("configuration"));
        node.set(&get_config("0.1.2"));

        assert_inspect_tree!(
            inspector,
            root: {
                configuration: {
                    updater: {
                        name: "Fuchsia",
                        version: "0.0.1.0",
                    },
                    os: {
                        platform: "Fuchsia",
                        version: "0.1.2",
                        service_pack: "",
                        arch: std::env::consts::ARCH,
                    },
                    omaha: {
                        service_url: "https://clients2.google.com/service/update2/fuchsia/json",
                    },
                }
            }
        );
    }

    #[test]
    fn test_apps_node() {
        let inspector = Inspector::new();
        let node = AppsNode::new(inspector.root().create_child("apps"));
        let apps = vec![
            App::new("id", [1, 0], Cohort::default()),
            App::new("id_2", [1, 2, 4], Cohort::new("cohort")),
        ];
        node.set(&apps);

        assert_inspect_tree!(
            inspector,
            root: {
                apps: {
                    apps: format!("{:?}", apps),
                }
            }
        );
    }

    #[test]
    fn test_state_node() {
        let inspector = Inspector::new();
        let node = StateNode::new(inspector.root().create_child("state"));
        let state = State {
            state: Some(ManagerState::CheckingForUpdates),
            version_available: Some("1.2.3.4".to_string()),
        };
        node.set(&state);

        assert_inspect_tree!(
            inspector,
            root: {
                state: {
                    state: format!("{:?}", state),
                }
            }
        );
    }
}
