// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::CreationManifestError;
use serde_derive::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::io;

/// A `CreationManifest` lists the files that should be included in a Fuchsia package.
/// Both `external_contents` and `far_contents` are maps from package relative paths in
/// the to-be-created package to paths on the local filesystem.
/// Package relative paths start with "meta/" if and only if they are in `far_contents`.
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
            VersionedCreationManifest::Version1(v1) => Ok(CreationManifest::from_v1(v1.check()?)),
        }
    }

    fn from_v1(v1: CreationManifestV1) -> Self {
        let mut far_contents = BTreeMap::new();
        for (package_path, host_path) in v1.far_contents.into_iter() {
            far_contents.insert(format!("meta/{}", package_path), host_path);
        }
        CreationManifest { external_contents: v1.external_contents, far_contents }
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

impl CreationManifestV1 {
    // Validate all package relative paths and make sure no external contents are in the
    // "meta/" package directory.
    fn check(self) -> Result<Self, CreationManifestError> {
        for (package_path, _) in self.external_contents.iter().chain(self.far_contents.iter()) {
            crate::path::check_package_path(&package_path).map_err(|e| {
                CreationManifestError::PackagePath { cause: e, path: package_path.clone() }
            })?;
        }
        for (package_path, _) in self.external_contents.iter() {
            if package_path.starts_with("meta/") {
                return Err(CreationManifestError::ExternalContentInMetaDirectory {
                    path: package_path.to_string(),
                });
            }
        }
        Ok(self)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::errors::PackagePathError::PathStartsWithSlash;
    use maplit::btreemap;
    use serde_json::json;

    /// Helper to assist asserting a single match branch.
    ///
    /// Ex:
    ///
    /// let arg = Arg::Uint(8);
    /// assert_matches!(arg, Arg::Uint(x) => assert_eq!(x, 8));
    macro_rules! assert_matches(
        ($e:expr, $p:pat => $a:expr) => (
            match $e {
                $p => $a,
                v => panic!("Failed to match '{:?}'", v),
            }
        )
    );

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
    fn test_invalid_package_path() {
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
            Err(CreationManifestError::PackagePath {
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
}
