// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the product_bundle metadata.

mod v1;
mod v2;

use serde::{Deserialize, Serialize};
pub use v1::*;
pub use v2::*;

/// Versioned product bundle metadata.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(untagged)]
pub enum ProductBundle {
    V1 { schema_id: String, data: ProductBundleV1 },
    V2(VersionedProductBundle),
}

/// The new system of versioning product bundles using the "version" tag.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(tag = "version")]
pub enum VersionedProductBundle {
    #[serde(rename = "2")]
    V2(ProductBundleV2),
}

const PRODUCT_BUNDLE_SCHEMA_V1: &str =
    "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json";

impl ProductBundle {
    /// Construct a new V1 ProductBundle.
    pub fn new_v1(v1: ProductBundleV1) -> Self {
        Self::V1 { schema_id: PRODUCT_BUNDLE_SCHEMA_V1.to_string(), data: v1 }
    }

    /// Returns ProductBundle entry name.
    pub fn name(&self) -> &str {
        match self {
            Self::V1 { schema_id: _, data } => &data.name.as_str(),
            Self::V2(version) => match version {
                VersionedProductBundle::V2(pb) => &pb.name.as_str(),
            },
        }
    }

    /// Get the list of logical device names.
    pub fn device_refs(&self) -> &Vec<String> {
        match self {
            Self::V1 { schema_id: _, data } => &data.device_refs,
            Self::V2(_version) => {
                panic!("no device_refs");
            }
        }
    }

    /// Manifest for the emulator, if present.
    pub fn emu_manifest(&self) -> &Option<EmuManifest> {
        match self {
            Self::V1 { schema_id: _, data } => &data.manifests.emu,
            Self::V2(_version) => {
                panic!("no emu_manifest");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{from_value, json};

    #[test]
    fn test_parse_v1() {
        let pb: ProductBundle = from_value(json!({
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
        assert!(matches!(pb, ProductBundle::V1 { .. }));
    }

    #[test]
    fn test_parse_v2() {
        let pb: ProductBundle = from_value(json!({
            "version": "2",
            "name": "generic-x64",
        }))
        .unwrap();
        assert!(matches!(pb, ProductBundle::V2 { .. }));
    }
}
