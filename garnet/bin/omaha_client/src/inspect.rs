// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{Node, Property, StringProperty};
use omaha_client::{
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::configuration::get_config;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};

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
}
