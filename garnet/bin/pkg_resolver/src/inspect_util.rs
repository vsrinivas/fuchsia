// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_pkg_ext::{MirrorConfigInspectState, RepositoryConfig},
    fuchsia_inspect::{self as inspect, NumericProperty, Property},
    fuchsia_inspect_contrib::inspectable::{Inspectable, Watch},
    fuchsia_url::pkg_url::RepoUrl,
    std::sync::Arc,
};

pub type InspectableRepositoryConfig =
    Inspectable<Arc<RepositoryConfig>, InspectableRepositoryConfigWatcher>;

pub struct InspectableRepositoryConfigWatcher {
    mirror_configs_node: inspect::Node,
    mirror_configs_states: Vec<MirrorConfigInspectState>,
    root_keys_node: inspect::Node,
    root_keys_properties: Vec<inspect::StringProperty>,
    update_package_url_property: inspect::StringProperty,
    _node: inspect::Node,
}

impl Watch<Arc<RepositoryConfig>> for InspectableRepositoryConfigWatcher {
    fn new(config: &Arc<RepositoryConfig>, node: &inspect::Node, name: impl AsRef<str>) -> Self {
        let repo_config_node = node.create_child(name);
        let mut ret = Self {
            root_keys_node: repo_config_node.create_child("root_keys"),
            root_keys_properties: vec![],
            mirror_configs_node: repo_config_node.create_child("mirrors"),
            mirror_configs_states: vec![],
            update_package_url_property: repo_config_node
                .create_string("update_package_url", format!("{:?}", config.update_package_url())),
            _node: repo_config_node,
        };
        ret.watch(config);
        ret
    }

    fn watch(&mut self, config: &Arc<RepositoryConfig>) {
        self.update_package_url_property.set(&format!("{:?}", config.update_package_url()));
        self.root_keys_properties = config
            .root_keys()
            .iter()
            .enumerate()
            .map(|(i, root_key)| {
                self.root_keys_node.create_string(&i.to_string(), format!("{:?}", root_key))
            })
            .collect();
        self.mirror_configs_states = config
            .mirrors()
            .iter()
            .enumerate()
            .map(|(i, mirror_config)| {
                mirror_config
                    .create_inspect_state(self.mirror_configs_node.create_child(&i.to_string()))
            })
            .collect();
    }
}

pub type InspectableRepoUrl = Inspectable<RepoUrl, InspectableRepoUrlWatcher>;

pub struct InspectableRepoUrlWatcher {
    url_property: inspect::StringProperty,
}

impl Watch<RepoUrl> for InspectableRepoUrlWatcher {
    fn new(repo_url: &RepoUrl, node: &inspect::Node, _name: impl AsRef<str>) -> Self {
        let url_property = node.create_string(format!("{:?}", repo_url.host()), "");

        Self { url_property }
    }

    fn watch(&mut self, _repo_url: &RepoUrl) {
        self.url_property.set("");
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

pub type OptionalTimeProperty = DebugStringProperty<Option<chrono::DateTime<chrono::offset::Utc>>>;

pub struct DebugStringProperty<D> {
    prop: inspect::StringProperty,
    _marker: std::marker::PhantomData<D>,
}

impl<D> DebugStringProperty<D>
where
    D: std::fmt::Debug,
{
    pub fn new(parent: &inspect::Node, name: &str, value: &D) -> Self {
        Self {
            prop: parent.create_string(name, format!("{:?}", value)),
            _marker: std::marker::PhantomData,
        }
    }

    pub fn set(&self, value: &D) {
        self.prop.set(&format!("{:?}", value));
    }
}

#[cfg(test)]
mod test_inspectable_repository_config {
    use {
        super::*,
        fidl_fuchsia_pkg_ext::{MirrorConfigBuilder, RepositoryConfigBuilder, RepositoryKey},
        fuchsia_inspect::assert_inspect_tree,
    };

    #[test]
    fn test_initialization() {
        let inspector = inspect::Inspector::new();
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let mirror_config = MirrorConfigBuilder::new("fake-mirror.com").build();
        let config = Arc::new(
            RepositoryConfigBuilder::new(fuchsia_url.clone())
                .add_root_key(RepositoryKey::Ed25519(vec![0]))
                .add_mirror(mirror_config.clone())
                .build(),
        );
        let inspectable =
            InspectableRepositoryConfig::new(config, inspector.root(), "test-property");

        assert_inspect_tree!(
            inspector,
            root: {
                "test-property": {
                  root_keys: {
                    "0": format!("{:?}", inspectable.root_keys()[0])
                  },
                  mirrors: {
                    "0": {
                        blob_key: format!("{:?}", mirror_config.blob_key()),
                        mirror_url: format!("{:?}", mirror_config.mirror_url()),
                        subscribe: format!("{:?}", mirror_config.subscribe()),
                        blob_mirror_url: format!("{:?}", mirror_config.blob_mirror_url())
                    }
                  },
                  update_package_url: format!("{:?}", inspectable.update_package_url()),
                }
            }
        );
    }

    #[test]
    fn test_watcher() {
        let inspector = inspect::Inspector::new();
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let config = Arc::new(
            RepositoryConfigBuilder::new(fuchsia_url.clone())
                .add_root_key(RepositoryKey::Ed25519(vec![0]))
                .build(),
        );
        let mirror_config = MirrorConfigBuilder::new("fake-mirror").build();
        let mut inspectable =
            InspectableRepositoryConfig::new(config, inspector.root(), "test-property");

        Arc::get_mut(&mut inspectable.get_mut())
            .expect("get repo config")
            .insert_mirror(mirror_config.clone());

        assert_inspect_tree!(
            inspector,
            root: {
                "test-property": {
                  root_keys: {
                    "0": format!("{:?}", inspectable.root_keys()[0])
                  },
                  mirrors: {
                    "0": {
                        blob_key: format!("{:?}", mirror_config.blob_key()),
                        mirror_url: format!("{:?}", mirror_config.mirror_url()),
                        subscribe: format!("{:?}", mirror_config.subscribe()),
                        blob_mirror_url: format!("{:?}", mirror_config.blob_mirror_url())
                    }
                  },
                  update_package_url: format!("{:?}", inspectable.update_package_url()),
                }
            }
        );
    }
}

#[cfg(test)]
mod test_inspectable_repo_url {
    use {super::*, fuchsia_inspect::assert_inspect_tree};

    #[test]
    fn test_initialization() {
        let inspector = inspect::Inspector::new();
        let fuchsia_url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let _inspectable = InspectableRepoUrl::new(fuchsia_url, inspector.root(), "test-property");

        assert_inspect_tree!(
            inspector,
            root: {
                "\"fuchsia.com\"": ""
            }
        );
    }
}
