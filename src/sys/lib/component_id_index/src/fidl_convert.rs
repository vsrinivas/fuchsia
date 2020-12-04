// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{AppmgrMoniker, Index, InstanceIdEntry};
use fidl_fuchsia_component_internal as fcomponent_internal;
use std::convert::TryFrom;
use thiserror::Error;

#[derive(Error, Debug, PartialEq)]
pub enum FidlConversionError {
    #[error("Missing appmgr_restrict_isolated_persistent_storage")]
    MissingAppmgrRestrictIsolatedPersistentStorage,
    #[error("Missing instances")]
    MissingInstances,
    #[error("Missing appmgr_moniker.url")]
    MissingAppmgrMonikerUrl,
    #[error("Missing appmgr_moniker.realm_path")]
    MissingAppmgrMonikerRealmPath,
}

// This converter translates between different encodings but does not do any semantic validation.
// To construct a validated Index, use a Index::from_*() constructor along with this converter.
impl TryFrom<fcomponent_internal::ComponentIdIndex> for Index {
    type Error = FidlConversionError;

    fn try_from(index: fcomponent_internal::ComponentIdIndex) -> Result<Self, Self::Error> {
        let appmgr_restrict_isolated_persistent_storage = Some(
            index
                .appmgr_restrict_isolated_persistent_storage
                .ok_or(FidlConversionError::MissingAppmgrRestrictIsolatedPersistentStorage)?,
        );
        let mut instances: Vec<InstanceIdEntry> = vec![];
        for entry in index.instances.ok_or_else(|| FidlConversionError::MissingInstances)? {
            instances.push(InstanceIdEntry {
                instance_id: entry.instance_id,
                appmgr_moniker: entry
                    .appmgr_moniker
                    .map(|appmgr_moniker| -> Result<AppmgrMoniker, Self::Error> {
                        Ok(AppmgrMoniker {
                            url: appmgr_moniker
                                .url
                                .ok_or_else(|| FidlConversionError::MissingAppmgrMonikerUrl)?,
                            realm_path: appmgr_moniker.realm_path.ok_or_else(|| {
                                FidlConversionError::MissingAppmgrMonikerRealmPath
                            })?,
                            transitional_realm_paths: appmgr_moniker.transitional_realm_paths,
                        })
                    })
                    .transpose()?,
                moniker: entry.moniker,
            });
        }
        Ok(Index { appmgr_restrict_isolated_persistent_storage, instances })
    }
}

impl From<Index> for fcomponent_internal::ComponentIdIndex {
    fn from(index: Index) -> Self {
        fcomponent_internal::ComponentIdIndex {
            appmgr_restrict_isolated_persistent_storage: Some(
                index.appmgr_restrict_isolated_persistent_storage.unwrap_or(true),
            ),
            instances: Some(
                index
                    .instances
                    .into_iter()
                    .map(|entry| fcomponent_internal::InstanceIdEntry {
                        instance_id: entry.instance_id,
                        appmgr_moniker: entry.appmgr_moniker.map(|appmgr_moniker| {
                            fcomponent_internal::AppmgrMoniker {
                                url: Some(appmgr_moniker.url),
                                realm_path: Some(appmgr_moniker.realm_path),
                                transitional_realm_paths: appmgr_moniker.transitional_realm_paths,
                                ..fcomponent_internal::AppmgrMoniker::empty()
                            }
                        }),
                        moniker: entry.moniker,
                        ..fcomponent_internal::InstanceIdEntry::empty()
                    })
                    .collect(),
            ),
            ..fcomponent_internal::ComponentIdIndex::empty()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fidl_native_translation() {
        let fidl_index = fcomponent_internal::ComponentIdIndex {
            appmgr_restrict_isolated_persistent_storage: Some(false),
            instances: Some(vec![fcomponent_internal::InstanceIdEntry {
                instance_id: Some("abc".to_string()),
                appmgr_moniker: Some(fcomponent_internal::AppmgrMoniker {
                    url: Some("abc".to_string()),
                    realm_path: Some(vec!["realm".to_string(), "path".to_string()]),
                    transitional_realm_paths: Some(vec![vec![
                        "transitional".to_string(),
                        "realm".to_string(),
                        "path".to_string(),
                    ]]),
                    ..fcomponent_internal::AppmgrMoniker::empty()
                }),
                moniker: Some("/a/b/c".to_string()),
                ..fcomponent_internal::InstanceIdEntry::empty()
            }]),
            ..fcomponent_internal::ComponentIdIndex::empty()
        };

        let native_index = Index {
            appmgr_restrict_isolated_persistent_storage: Some(false),
            instances: vec![InstanceIdEntry {
                instance_id: Some("abc".to_string()),
                appmgr_moniker: Some(AppmgrMoniker {
                    url: "abc".to_string(),
                    realm_path: vec!["realm".to_string(), "path".to_string()],
                    transitional_realm_paths: Some(vec![vec![
                        "transitional".to_string(),
                        "realm".to_string(),
                        "path".to_string(),
                    ]]),
                }),
                moniker: Some("/a/b/c".to_string()),
            }],
        };

        assert_eq!(native_index, Index::try_from(fidl_index.clone()).unwrap());
        assert_eq!(fidl_index, fcomponent_internal::ComponentIdIndex::from(native_index));
    }

    #[test]
    fn missing_appmgr_restrict_isolated_persistent_storage() {
        assert_eq!(
            Some(FidlConversionError::MissingAppmgrRestrictIsolatedPersistentStorage),
            Index::try_from(fcomponent_internal::ComponentIdIndex::empty()).err()
        );
    }

    #[test]
    fn missing_instances() {
        assert_eq!(
            Some(FidlConversionError::MissingInstances),
            Index::try_from(fcomponent_internal::ComponentIdIndex {
                appmgr_restrict_isolated_persistent_storage: Some(true),
                ..fcomponent_internal::ComponentIdIndex::empty()
            })
            .err()
        );
    }

    #[test]
    fn missing_appmgr_moniker_url() {
        assert_eq!(
            Some(FidlConversionError::MissingAppmgrMonikerUrl),
            Index::try_from(fcomponent_internal::ComponentIdIndex {
                appmgr_restrict_isolated_persistent_storage: Some(true),
                instances: Some(vec![fcomponent_internal::InstanceIdEntry {
                    instance_id: Some("abc".to_string()),
                    appmgr_moniker: Some(fcomponent_internal::AppmgrMoniker {
                        url: None,
                        ..fcomponent_internal::AppmgrMoniker::empty()
                    }),
                    ..fcomponent_internal::InstanceIdEntry::empty()
                }]),
                ..fcomponent_internal::ComponentIdIndex::empty()
            })
            .err()
        );
    }

    #[test]
    fn missing_appmgr_moniker_realm_path() {
        assert_eq!(
            Some(FidlConversionError::MissingAppmgrMonikerRealmPath),
            Index::try_from(fcomponent_internal::ComponentIdIndex {
                appmgr_restrict_isolated_persistent_storage: Some(true),
                instances: Some(vec![fcomponent_internal::InstanceIdEntry {
                    instance_id: Some("abc".to_string()),
                    appmgr_moniker: Some(fcomponent_internal::AppmgrMoniker {
                        url: Some("abc".to_string()),
                        ..fcomponent_internal::AppmgrMoniker::empty()
                    }),
                    ..fcomponent_internal::InstanceIdEntry::empty()
                }]),
                ..fcomponent_internal::ComponentIdIndex::empty()
            })
            .err()
        );
    }
}
