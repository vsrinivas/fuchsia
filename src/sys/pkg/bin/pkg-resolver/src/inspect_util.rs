// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_pkg_ext::{MirrorConfig, RepositoryConfig},
    fuchsia_inspect::{self as inspect, NumericProperty, StringReference},
    fuchsia_inspect_contrib::inspectable::{Inspectable, Watch},
    std::sync::Arc,
};

pub type InspectableRepositoryConfig =
    Inspectable<Arc<RepositoryConfig>, InspectableRepositoryConfigWatcher>;

pub struct InspectableRepositoryConfigWatcher {
    mirror_configs_node: inspect::Node,
    _mirror_configs_states: Vec<MirrorConfigInspectState>,
    root_keys_node: inspect::Node,
    _root_keys_properties: Vec<inspect::StringProperty>,
    _node: inspect::Node,
}

impl Watch<Arc<RepositoryConfig>> for InspectableRepositoryConfigWatcher {
    fn new<'a>(
        config: &Arc<RepositoryConfig>,
        node: &inspect::Node,
        name: impl Into<StringReference<'a>>,
    ) -> Self {
        let repo_config_node = node.create_child(name);
        let mut ret = Self {
            root_keys_node: repo_config_node.create_child("root_keys"),
            _root_keys_properties: vec![],
            mirror_configs_node: repo_config_node.create_child("mirrors"),
            _mirror_configs_states: vec![],
            _node: repo_config_node,
        };
        ret.watch(config);
        ret
    }

    fn watch(&mut self, config: &Arc<RepositoryConfig>) {
        self._root_keys_properties = config
            .root_keys()
            .iter()
            .enumerate()
            .map(|(i, root_key)| {
                self.root_keys_node.create_string(&i.to_string(), format!("{:?}", root_key))
            })
            .collect();
        self._mirror_configs_states = config
            .mirrors()
            .iter()
            .enumerate()
            .map(|(i, mirror_config)| {
                MirrorConfigInspectState::new(
                    mirror_config,
                    self.mirror_configs_node.create_child(&i.to_string()),
                )
            })
            .collect();
    }
}

#[derive(Debug)]
pub struct Counter {
    prop: inspect::UintProperty,
}

impl Counter {
    pub fn new(parent: &inspect::Node, name: &str) -> Self {
        Self { prop: parent.create_uint(name, 0) }
    }

    pub fn increment(&self) {
        self.prop.add(1);
    }
}

pub struct MirrorConfigInspectState {
    _mirror_url_property: inspect::StringProperty,
    _subscribe_property: inspect::StringProperty,
    _blob_mirror_url_property: inspect::StringProperty,
    _node: inspect::Node,
}

impl MirrorConfigInspectState {
    fn new(mirror_config: &MirrorConfig, node: inspect::Node) -> Self {
        MirrorConfigInspectState {
            _mirror_url_property: node
                .create_string("mirror_url", format!("{:?}", mirror_config.mirror_url())),
            _subscribe_property: node
                .create_string("subscribe", format!("{:?}", &mirror_config.subscribe())),
            _blob_mirror_url_property: node
                .create_string("blob_mirror_url", format!("{:?}", mirror_config.blob_mirror_url())),
            _node: node,
        }
    }
}

#[cfg(test)]
mod test_inspectable_repository_config {
    use {
        super::*,
        fidl_fuchsia_pkg_ext::{MirrorConfigBuilder, RepositoryConfigBuilder, RepositoryKey},
        fuchsia_inspect::assert_data_tree,
        http::Uri,
    };

    #[test]
    fn test_initialization() {
        let inspector = inspect::Inspector::new();
        let fuchsia_url = fuchsia_url::RepositoryUrl::parse("fuchsia-pkg://fuchsia.com/").unwrap();
        let mirror_config =
            MirrorConfigBuilder::new("http://fake-mirror.com".parse::<Uri>().unwrap())
                .unwrap()
                .build();
        let config = Arc::new(
            RepositoryConfigBuilder::new(fuchsia_url)
                .add_root_key(RepositoryKey::Ed25519(vec![0]))
                .add_mirror(mirror_config.clone())
                .build(),
        );
        let inspectable =
            InspectableRepositoryConfig::new(config, inspector.root(), "test-property");

        assert_data_tree!(
            inspector,
            root: {
                "test-property": {
                  root_keys: {
                    "0": format!("{:?}", inspectable.root_keys()[0])
                  },
                  mirrors: {
                    "0": {
                        mirror_url: format!("{:?}", mirror_config.mirror_url()),
                        subscribe: format!("{:?}", mirror_config.subscribe()),
                        blob_mirror_url: format!("{:?}", mirror_config.blob_mirror_url())
                    }
                  },
                }
            }
        );
    }

    #[test]
    fn test_watcher() {
        let inspector = inspect::Inspector::new();
        let fuchsia_url = fuchsia_url::RepositoryUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let config = Arc::new(
            RepositoryConfigBuilder::new(fuchsia_url)
                .add_root_key(RepositoryKey::Ed25519(vec![0]))
                .build(),
        );
        let mirror_config =
            MirrorConfigBuilder::new("http://fake-mirror.com".parse::<Uri>().unwrap())
                .unwrap()
                .build();
        let mut inspectable =
            InspectableRepositoryConfig::new(config, inspector.root(), "test-property");

        Arc::get_mut(&mut inspectable.get_mut())
            .expect("get repo config")
            .insert_mirror(mirror_config.clone());

        assert_data_tree!(
            inspector,
            root: {
                "test-property": {
                  root_keys: {
                    "0": format!("{:?}", inspectable.root_keys()[0])
                  },
                  mirrors: {
                    "0": {
                        mirror_url: format!("{:?}", mirror_config.mirror_url()),
                        subscribe: format!("{:?}", mirror_config.subscribe()),
                        blob_mirror_url: format!("{:?}", mirror_config.blob_mirror_url())
                    }
                  },
                }
            }
        );
    }
}
