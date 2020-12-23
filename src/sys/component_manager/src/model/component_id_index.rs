// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    clonable_error::ClonableError,
    component_id_index, fidl_fuchsia_component_internal as fcomponent_internal, io_util,
    moniker::{AbsoluteMoniker, MonikerError},
    std::collections::HashMap,
    thiserror::Error,
};

pub type ComponentInstanceId = String;

#[derive(Debug, Clone, Error)]
pub enum ComponentIdIndexError {
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

/// ComponentIdIndex parses a given index and provides methods to look up instance IDs.
#[derive(Debug, Default)]
pub struct ComponentIdIndex {
    _moniker_to_instance_id: HashMap<AbsoluteMoniker, ComponentInstanceId>,
}

impl ComponentIdIndex {
    pub async fn new(index_file_path: &str) -> Result<Self, ComponentIdIndexError> {
        let fidl_index = io_util::file::read_in_namespace_to_fidl::<
            fcomponent_internal::ComponentIdIndex,
        >(index_file_path)
        .await
        .map_err(|err| ComponentIdIndexError::IndexUnreadable {
            path: index_file_path.to_string(),
            err: err.into(),
        })?;

        let index = component_id_index::Index::from_fidl(fidl_index)?;

        let mut moniker_to_instance_id = HashMap::<AbsoluteMoniker, ComponentInstanceId>::new();
        for entry in &index.instances {
            if let Some(absolute_moniker) = &entry.moniker {
                // let absolute_moniker = AbsoluteMoniker::parse_string_without_instances(moniker)
                //     .map_err(|e| ComponentIdIndexError::MonikerError(e))?;
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

    pub fn look_up_moniker(&self, moniker: &AbsoluteMoniker) -> Option<&ComponentInstanceId> {
        self._moniker_to_instance_id.get(moniker)
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use anyhow::Result;
    use fidl::encoding::encode_persistent;
    use fuchsia_async as fasync;
    use std::convert::TryFrom;
    use std::io::Write;
    use tempfile::NamedTempFile;

    fn make_index_file(index: component_id_index::Index) -> Result<NamedTempFile> {
        let mut tmp_file = NamedTempFile::new()?;
        tmp_file.write_all(
            encode_persistent(&mut fcomponent_internal::ComponentIdIndex::try_from(index)?)?
                .as_ref(),
        )?;
        Ok(tmp_file)
    }

    #[fasync::run_singlethreaded(test)]
    async fn look_up_moniker_no_exists() {
        let index_file = make_index_file(component_id_index::Index::default()).unwrap();
        let index = ComponentIdIndex::new(index_file.path().to_str().unwrap()).await.unwrap();
        assert!(index
            .look_up_moniker(&AbsoluteMoniker::parse_string_without_instances("/a/b/c").unwrap())
            .is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn look_up_moniker_exists() {
        let iid = "0".repeat(64);
        let index_file = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: Some(iid.clone()),
                appmgr_moniker: None,
                moniker: Some(AbsoluteMoniker::parse_string_without_instances("/a/b/c").unwrap()),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();
        let index = ComponentIdIndex::new(index_file.path().to_str().unwrap()).await.unwrap();
        assert_eq!(
            Some(&iid),
            index.look_up_moniker(
                &AbsoluteMoniker::parse_string_without_instances("/a/b/c").unwrap()
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn index_unreadable() {
        let result = ComponentIdIndex::new("/this/path/doesnt/exist").await;
        assert!(matches!(result, Err(ComponentIdIndexError::IndexUnreadable { path: _, err: _ })));
    }
}
