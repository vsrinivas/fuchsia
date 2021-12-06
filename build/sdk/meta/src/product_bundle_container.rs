// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the sdk fms container which holds all the artifacts that
//! make up a release of the Fuchsia SDK.

use {
    crate::{
        common::{ElementType, Envelope},
        json::{schema, JsonObject},
        ProductBundleV1,
    },
    serde::{Deserialize, Serialize},
};

/// TODO(b/205780240): Remove this "data" wrapper.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct WorkaroundProductBundleWrapper {
    pub data: ProductBundleV1,
    pub schema_id: String,
}

/// Description of a FMS container file that collects many instances of FMS
/// metadata.
///
/// This does not include the data "envelope", i.e. it begins within /data in
/// the source json file.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductBundleContainerV1 {
    /// A unique name identifying the instance.
    pub name: String,

    /// An optional human readable description.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,

    /// Always "product_bundle_container" for a ProductBundleContainerV1. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub kind: ElementType,

    /// A collection of product bundle instances (this may expand to more data
    /// types).
    pub bundles: Vec<WorkaroundProductBundleWrapper>,
}

impl JsonObject for Envelope<ProductBundleContainerV1> {
    fn get_schema() -> &'static str {
        include_str!("../product_bundle_container-76a5c104.json")
    }

    fn get_referenced_schemata() -> &'static [&'static str] {
        &[
            schema::COMMON,
            schema::HARDWARE_V1,
            schema::EMU_MANIFEST,
            schema::FLASH_MANIFEST_V1,
            schema::PRODUCT_BUNDLE_COMMON_V1,
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    test_validation! {
        name = test_validation,
        kind = Envelope::<ProductBundleContainerV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-76a5c104.json",
            "data": {
                "name": "Fuchsia F1",
                "type": "product_bundle_container",
                "bundles": [
                    {
                        "data": {
                            "name": "generic-x64",
                            "type": "product_bundle",
                            "device_refs": ["generic-x64"],
                            "images": [{
                                "base_uri": "gs://fuchsia/development/0.20201216.2.1/images/generic-x64.tgz",
                                "format": "tgz"
                            }],
                            "packages": [{
                                "format": "tgz",
                                "repo_uri": "gs://fuchsia/development/0.20201216.2.1/packages/generic-x64.tar.gz"
                            }]
                        },
                        "schema_id": "product_bundle_common-ab8943fd.json#/definitions/product_bundle"
                    }
                ]
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = Envelope::<ProductBundleContainerV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-76a5c104.json",
            "data": {
                "name": "Fuchsia F1",
                "type": "cc_prebuilt_library",
                "bundles": []
            }
        }
        "#,
        // Incorrect type
        valid = false,
    }
}
