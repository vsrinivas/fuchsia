// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the product_bundle metadata.

mod v1;
mod v2;

use anyhow::{Context, Result};
use camino::Utf8Path;
use serde::{Deserialize, Serialize};
use std::fs::File;
pub use v1::*;
pub use v2::{ProductBundleV2, Repository};

/// Versioned product bundle.
#[derive(Clone, Debug, PartialEq)]
pub enum ProductBundle {
    V1(ProductBundleV1),
    V2(ProductBundleV2),
}

/// Private helper for serializing the ProductBundle. A ProductBundle cannot be deserialized
/// without going through `try_from_path` in order to require that we use this helper, and the
/// `directory` field gets populated.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(untagged)]
enum SerializationHelper {
    V1 { schema_id: String, data: ProductBundleV1 },
    V2(SerializationHelperVersioned),
}

/// Helper for serializing the new system of versioning product bundles using the "version" tag.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(tag = "version")]
enum SerializationHelperVersioned {
    #[serde(rename = "2")]
    V2(ProductBundleV2),
}

const PRODUCT_BUNDLE_SCHEMA_V1: &str =
    "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json";

impl ProductBundle {
    /// Load a ProductBundle from a path on disk.
    pub fn try_load_from(path: impl AsRef<Utf8Path>) -> Result<Self> {
        let product_bundle_path = path.as_ref().join("product_bundle.json");
        let file = File::open(&product_bundle_path)
            .with_context(|| format!("opening product bundle: {:?}", &product_bundle_path))?;
        let helper: SerializationHelper =
            serde_json::from_reader(file).context("parsing product bundle")?;
        match helper {
            SerializationHelper::V1 { schema_id: _, data } => Ok(ProductBundle::V1(data)),
            SerializationHelper::V2(SerializationHelperVersioned::V2(data)) => {
                let mut data = data.clone();
                data.canonicalize_paths(path.as_ref())?;
                Ok(ProductBundle::V2(data))
            }
        }
    }

    /// Write a product bundle to a directory on disk at `path`.
    /// Note that this only writes the manifest file, and not the artifacts, images, blobs.
    pub fn write(&self, path: impl AsRef<Utf8Path>) -> Result<()> {
        let helper = match self {
            Self::V1(data) => SerializationHelper::V1 {
                schema_id: PRODUCT_BUNDLE_SCHEMA_V1.into(),
                data: data.clone(),
            },
            Self::V2(data) => {
                let mut data = data.clone();
                data.relativize_paths(path.as_ref())?;
                SerializationHelper::V2(SerializationHelperVersioned::V2(data))
            }
        };
        let product_bundle_path = path.as_ref().join("product_bundle.json");
        let file = File::create(product_bundle_path).context("creating product bundle file")?;
        serde_json::to_writer(file, &helper).context("writing product bundle file")?;
        Ok(())
    }

    /// Returns ProductBundle entry name.
    pub fn name(&self) -> &str {
        match self {
            Self::V1(data) => &data.name.as_str(),
            Self::V2(_) => panic!("no product name"),
        }
    }

    /// Get the list of logical device names.
    pub fn device_refs(&self) -> &Vec<String> {
        match self {
            Self::V1(data) => &data.device_refs,
            Self::V2(_) => panic!("no device_refs"),
        }
    }

    /// Manifest for the emulator, if present.
    pub fn emu_manifest(&self) -> &Option<EmuManifest> {
        match self {
            Self::V1(data) => &data.manifests.emu,
            Self::V2(_) => panic!("no emu_manifest"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use tempfile::TempDir;

    #[test]
    fn test_parse_v1() {
        let tmp = TempDir::new().unwrap();
        let pb_dir = Utf8Path::from_path(tmp.path()).unwrap();

        let pb_file = File::create(pb_dir.join("product_bundle.json")).unwrap();
        serde_json::to_writer(&pb_file, &json!({
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
            "data": {
                "name": "generic-x64",
                "type": "product_bundle",
                "device_refs": ["generic-x64"],
                "images": [{
                    "base_uri": "file://fuchsia/development/0.20201216.2.1/images/generic-x64.tgz",
                    "format": "tgz"
                }],
                "manifests": {
                },
                "packages": [{
                    "format": "tgz",
                    "repo_uri": "file://fuchsia/development/0.20201216.2.1/packages/generic-x64.tar.gz"
                }]
            }
        })).unwrap();
        let pb = ProductBundle::try_load_from(pb_dir).unwrap();
        assert!(matches!(pb, ProductBundle::V1 { .. }));
    }

    #[test]
    fn test_parse_v2() {
        let tmp = TempDir::new().unwrap();
        let pb_dir = Utf8Path::from_path(tmp.path()).unwrap();

        let pb_file = File::create(pb_dir.join("product_bundle.json")).unwrap();
        serde_json::to_writer(
            &pb_file,
            &json!({
                "version": "2",
                "partitions": {
                    "hardware_revision": "board",
                    "bootstrap_partitions": [],
                    "bootloader_partitions": [],
                    "partitions": [],
                    "unlock_credentials": [],
                },
            }),
        )
        .unwrap();
        let pb = ProductBundle::try_load_from(pb_dir).unwrap();
        assert!(matches!(pb, ProductBundle::V2 { .. }));
    }
}
