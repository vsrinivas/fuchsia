// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow,
    clonable_error::ClonableError,
    component_id_index, fidl,
    fidl::encoding::decode_persistent,
    fidl_fuchsia_component_internal as fcomponent_internal,
    moniker::{MonikerError, PartialAbsoluteMoniker},
    std::collections::HashMap,
    thiserror::Error,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

pub type ComponentInstanceId = String;

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Debug, Clone, Error)]
pub enum ComponentIdIndexError {
    // The capability routing static analyzer does not report this error subtype as part of a
    // routing verification result, so we don't need to serialize it.
    #[cfg_attr(feature = "serde", serde(skip))]
    #[error("could not read index file {}", .path)]
    IndexUnreadable {
        #[source]
        err: ClonableError,
        path: String,
    },
    #[error("Index error")]
    IndexError(#[from] component_id_index::IndexError),
    #[error("invalid moniker")]
    MonikerError(#[from] MonikerError),
}

impl ComponentIdIndexError {
    pub fn index_unreadable(
        index_file_path: impl Into<String>,
        err: impl Into<anyhow::Error>,
    ) -> Self {
        ComponentIdIndexError::IndexUnreadable {
            path: index_file_path.into(),
            err: err.into().into(),
        }
    }
}

/// ComponentIdIndex parses a given index and provides methods to look up instance IDs.
#[derive(Debug, Default)]
pub struct ComponentIdIndex {
    /// Map of a moniker from the index to its instance ID.
    ///
    /// The moniker does not contain instances, i.e. all of the ChildMonikers in the
    /// path have the (moniker, not index) instance ID set to zero.
    _moniker_to_instance_id: HashMap<PartialAbsoluteMoniker, ComponentInstanceId>,
}

impl ComponentIdIndex {
    pub async fn new(index_file_path: &str) -> Result<Self, ComponentIdIndexError> {
        let raw_content = std::fs::read(index_file_path)
            .map_err(|err| ComponentIdIndexError::index_unreadable(index_file_path, err))?;

        let fidl_index =
            decode_persistent::<fcomponent_internal::ComponentIdIndex>(&raw_content)
                .map_err(|err| ComponentIdIndexError::index_unreadable(index_file_path, err))?;

        let index = component_id_index::Index::from_fidl(fidl_index)?;

        let mut moniker_to_instance_id =
            HashMap::<PartialAbsoluteMoniker, ComponentInstanceId>::new();
        for entry in &index.instances {
            if let Some(absolute_moniker) = &entry.moniker {
                moniker_to_instance_id.insert(
                    absolute_moniker.clone(),
                    entry
                        .instance_id
                        .as_ref()
                        .ok_or_else(|| {
                            ComponentIdIndexError::IndexError(
                                component_id_index::IndexError::ValidationError(
                                    component_id_index::ValidationError::MissingInstanceIds {
                                        entries: vec![entry.clone()],
                                    },
                                ),
                            )
                        })?
                        .clone(),
                );
            }
        }
        Ok(Self { _moniker_to_instance_id: moniker_to_instance_id })
    }

    pub fn look_up_moniker(
        &self,
        moniker: &PartialAbsoluteMoniker,
    ) -> Option<&ComponentInstanceId> {
        self._moniker_to_instance_id.get(&moniker)
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use moniker::{AbsoluteMonikerBase, PartialChildMoniker};
    use routing_test_helpers::component_id_index::make_index_file;

    #[fuchsia::test]
    async fn look_up_moniker_no_exists() {
        let index_file = make_index_file(component_id_index::Index::default()).unwrap();
        let index = ComponentIdIndex::new(index_file.path().to_str().unwrap()).await.unwrap();
        assert!(index
            .look_up_moniker(
                &PartialAbsoluteMoniker::parse_string_without_instances("/a/b/c").unwrap()
            )
            .is_none());
    }

    #[fuchsia::test]
    async fn look_up_moniker_exists() {
        let iid = "0".repeat(64);
        let index_file = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: Some(iid.clone()),
                appmgr_moniker: None,
                moniker: Some(
                    PartialAbsoluteMoniker::parse_string_without_instances("/a/b/c").unwrap(),
                ),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();
        let index = ComponentIdIndex::new(index_file.path().to_str().unwrap()).await.unwrap();
        assert_eq!(
            Some(&iid),
            index.look_up_moniker(
                &PartialAbsoluteMoniker::parse_string_without_instances("/a/b/c").unwrap()
            )
        );
    }

    #[fuchsia::test]
    async fn look_up_moniker_with_instances_exists() {
        let iid = "0".repeat(64);
        let index_file = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: Some(iid.clone()),
                appmgr_moniker: None,
                moniker: Some(
                    PartialAbsoluteMoniker::parse_string_without_instances("/a/coll:name").unwrap(),
                ),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();
        let index = ComponentIdIndex::new(index_file.path().to_str().unwrap()).await.unwrap();
        assert_eq!(
            Some(&iid),
            index.look_up_moniker(&PartialAbsoluteMoniker::new(vec![
                PartialChildMoniker::new("a".to_string(), None),
                PartialChildMoniker::new("name".to_string(), Some("coll".to_string())),
            ]))
        );
    }

    #[fuchsia::test]
    async fn index_unreadable() {
        let result = ComponentIdIndex::new("/this/path/doesnt/exist").await;
        assert!(matches!(result, Err(ComponentIdIndexError::IndexUnreadable { path: _, err: _ })));
    }
}
