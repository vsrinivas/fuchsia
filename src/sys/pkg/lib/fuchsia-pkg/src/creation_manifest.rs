// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::CreationManifestError;
use serde_derive::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::io;

/// A `CreationManifest` lists the files that should be included in a Fuchsia package.
/// Both `external_contents` and `far_contents` are maps from package resource paths in
/// the to-be-created package to paths on the local filesystem.
/// Package resource paths start with "meta/" if and only if they are in `far_contents`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CreationManifest {
    external_contents: BTreeMap<String, String>,
    far_contents: BTreeMap<String, String>,
}

impl CreationManifest {
    /// `external_contents` lists the blobs that make up the meta/contents file
    pub fn external_contents(&self) -> &BTreeMap<String, String> {
        &self.external_contents
    }

    /// `far_contents` lists the files to be included bodily in the meta.far
    pub fn far_contents(&self) -> &BTreeMap<String, String> {
        &self.far_contents
    }

    /// Creates a `CreationManifest` from external and far contents maps.
    ///
    /// `external_contents` is a map from package resource paths to their locations
    /// on the host filesystem. These are the files that will be listed in
    /// `meta/contents`. Resource paths in `external_contents` must *not* start with
    /// `meta/`.
    /// `far_contents` is a map from package resource paths to their locations
    /// on the host filesystem. These are the files that will be included bodily in the
    /// package `meta.far` archive. Resource paths in `far_contents` must start with
    /// `meta/`.
    ///
    /// # Examples
    ///
    /// ```
    /// # use fuchsia_pkg::CreationManifest;
    /// # use maplit::btreemap;
    /// let external_contents = btreemap! {
    ///     "lib/mylib.so".to_string() => "build/system/path/mylib.so".to_string()
    /// };
    /// let far_contents = btreemap! {
    ///     "meta/my_component_manifest.cmx".to_string() =>
    ///         "other/build/system/path/my_component_manifest.cmx".to_string()
    /// };
    /// let creation_manifest =
    ///     CreationManifest::from_external_and_far_contents(external_contents, far_contents)
    ///         .unwrap();
    /// ```
    pub fn from_external_and_far_contents(
        external_contents: BTreeMap<String, String>,
        far_contents: BTreeMap<String, String>,
    ) -> Result<Self, CreationManifestError> {
        for (resource_path, _) in external_contents.iter().chain(far_contents.iter()) {
            crate::path::check_resource_path(&resource_path).map_err(|e| {
                CreationManifestError::ResourcePath { cause: e, path: resource_path.to_string() }
            })?;
        }
        for (resource_path, _) in external_contents.iter() {
            if resource_path.starts_with("meta/") {
                return Err(CreationManifestError::ExternalContentInMetaDirectory {
                    path: resource_path.to_string(),
                });
            }
        }
        for (resource_path, _) in far_contents.iter() {
            if !resource_path.starts_with("meta/") {
                return Err(CreationManifestError::FarContentNotInMetaDirectory {
                    path: resource_path.to_string(),
                });
            }
        }
        Ok(CreationManifest { external_contents, far_contents })
    }

    /// Deserializes a `CreationManifest` from versioned json.
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_pkg::CreationManifest;
    /// let json_string = r#"
    /// {
    ///   "version": "1",
    ///   "content": {
    ///     "/": {
    ///       "lib/mylib.so": "build/system/path/mylib.so"
    ///     },
    ///    "/meta/": {
    ///      "my_component_manifest.cmx": "other/build/system/path/my_component_manifest.cmx"
    ///    }
    /// }"#;
    /// let creation_manifest = CreationManifest::from_json(json_string.as_bytes());
    /// ```
    pub fn from_json<R: io::Read>(reader: R) -> Result<Self, CreationManifestError> {
        match serde_json::from_reader::<R, VersionedCreationManifest>(reader)? {
            VersionedCreationManifest::Version1(v1) => CreationManifest::from_v1(v1),
        }
    }

    fn from_v1(v1: CreationManifestV1) -> Result<Self, CreationManifestError> {
        let mut far_contents = BTreeMap::new();
        // Validate package resource paths in far contents before "meta/" is prepended
        // for better error messages.
        for (resource_path, host_path) in v1.far_contents.into_iter() {
            crate::path::check_resource_path(&resource_path).map_err(|e| {
                CreationManifestError::ResourcePath { cause: e, path: resource_path.to_string() }
            })?;
            far_contents.insert(format!("meta/{}", resource_path), host_path);
        }
        CreationManifest::from_external_and_far_contents(v1.external_contents, far_contents)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum VersionedCreationManifest {
    #[serde(rename = "1")]
    Version1(CreationManifestV1),
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct CreationManifestV1 {
    #[serde(rename = "/")]
    external_contents: BTreeMap<String, String>,
    #[serde(rename = "/meta/")]
    far_contents: BTreeMap<String, String>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::errors::ResourcePathError::PathStartsWithSlash;
    use crate::test::*;
    use maplit::btreemap;
    use proptest::prelude::*;
    use serde_json::json;

    fn from_json_value(
        value: serde_json::Value,
    ) -> Result<CreationManifest, CreationManifestError> {
        CreationManifest::from_json(value.to_string().as_bytes())
    }

    #[test]
    fn test_malformed_json() {
        assert_matches!(
            CreationManifest::from_json("<invalid json document>".as_bytes()),
            Err(CreationManifestError::Json(err)) => assert!(err.is_syntax())
        );
    }

    #[test]
    fn test_invalid_version() {
        assert_matches!(
            from_json_value(json!({"version": "2", "content": {}})),
            Err(CreationManifestError::Json(err)) => assert!(err.is_data()));
    }

    #[test]
    fn test_invalid_resource_path() {
        assert_matches!(
            from_json_value(
                json!(
                    {"version": "1",
                     "content":
                     {"/meta/" :
                      {"/starts-with-slash": "host-path"},
                      "/": {
                      }
                     }
                    }
                )
            ),
            Err(CreationManifestError::ResourcePath {
                cause: PathStartsWithSlash,
                path: s
            })
                => assert_eq!(s, "/starts-with-slash"));
    }

    #[test]
    fn test_meta_in_external() {
        assert_matches!(
            from_json_value(
                json!(
                    {"version": "1",
                     "content":
                     {"/meta/" : {},
                      "/": {
                          "meta/foo": "host-path"}
                     }
                    }
                )
            ),
            Err(CreationManifestError::ExternalContentInMetaDirectory{path: s})
                => assert_eq!(s, "meta/foo"));
    }

    #[test]
    fn test_from_v1() {
        assert_eq!(
            from_json_value(json!(
                {"version": "1",
                 "content": {
                     "/": {
                         "this-path": "this-host-path",
                         "that/path": "that/host/path"},
                     "/meta/" : {
                         "some-path": "some-host-path",
                         "other/path": "other/host/path"}
                 }
                }
            ))
            .unwrap(),
            CreationManifest {
                external_contents: btreemap! {
                    "this-path".to_string() => "this-host-path".to_string(),
                    "that/path".to_string() => "that/host/path".to_string()
                },
                far_contents: btreemap! {
                    "meta/some-path".to_string() => "some-host-path".to_string(),
                    "meta/other/path".to_string() => "other/host/path".to_string()
                }
            }
        );
    }

    proptest! {
        #[test]
        fn test_from_external_and_far_contents_does_not_modify_valid_maps(
            ref external_resource_path in random_resource_path(1, 3),
            ref external_host_path in ".{0,30}",
            ref far_resource_path in random_resource_path(1, 3),
            ref far_host_path in ".{0,30}"
        ) {
            prop_assume!(!external_resource_path.starts_with("meta/"));
            let external_contents = btreemap! {
                external_resource_path.to_string() => external_host_path.to_string()
            };
            let far_resource_path = format!("meta/{}", far_resource_path);
            let far_contents = btreemap! {
                far_resource_path.to_string() => far_host_path.to_string()
            };

            let creation_manifest = CreationManifest::from_external_and_far_contents(
                external_contents.clone(), far_contents.clone())
                .unwrap();

            prop_assert_eq!(creation_manifest.external_contents(), &external_contents);
            prop_assert_eq!(creation_manifest.far_contents(), &far_contents);
        }
    }
}
