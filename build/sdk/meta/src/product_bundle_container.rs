// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the sdk fms container which holds all the artifacts that
//! make up a release of the Fuchsia SDK.

use {
    crate::{
        common::{ElementType, Envelope},
        json::{schema, JsonObject},
        metadata::Metadata,
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
            schema::PRODUCT_BUNDLE_V1,
        ]
    }
}

/// Description of a FMS container file that collects many instances of FMS
/// metadata.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductBundleContainerV2 {
    /// A unique name identifying the instance.
    pub name: String,

    /// An optional human readable description.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,

    /// Always "product_bundle_container" for a ProductBundleContainerV2. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub kind: ElementType,

    /// A collection of FMS entries of various types including product bundles and virtual
    /// device specs.
    pub fms_entries: Vec<Metadata>,
}

impl JsonObject for Envelope<ProductBundleContainerV2> {
    fn get_schema() -> &'static str {
        include_str!("../product_bundle_container-32z5e391.json")
    }

    fn get_referenced_schemata() -> &'static [&'static str] {
        &[
            schema::COMMON,
            schema::HARDWARE_V1,
            schema::EMU_MANIFEST,
            schema::FLASH_MANIFEST_V1,
            schema::PRODUCT_BUNDLE_V1,
            schema::PHYSICAL_DEVICE_V1,
            schema::VIRTUAL_DEVICE_V1,
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    test_validation! {
        name = test_validation_v1,
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
                        "schema_id": "product_bundle-6320eef1.json#/definitions/product_bundle"
                    }
                ]
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_v1_invalid,
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

    test_validation! {
        name = test_validation_v2_pbm,
        kind = Envelope::<ProductBundleContainerV2>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-32z5e391.json",
            "data": {
                "name": "PBM container",
                "type": "product_bundle_container",
                "fms_entries": [
                    {
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
                    }
                ]
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_v2_virt_device,
        kind = Envelope::<ProductBundleContainerV2>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-32z5e391.json",
            "data": {
                "name": "Virtual device container",
                "type": "product_bundle_container",
                "fms_entries": [
                    {
                        "name": "generic-x64",
                        "type": "virtual_device",
                        "hardware": {
                            "audio": {
                                "model": "hda"
                            },
                            "cpu": {
                                "arch": "x64"
                            },
                            "inputs": {
                                "pointing_device": "touch"
                            },
                            "window_size": {
                                "width": 640,
                                "height": 480,
                                "units": "pixels"
                            },
                            "memory": {
                                "quantity": 1,
                                "units": "gigabytes"
                            },
                            "storage": {
                                "quantity": 1,
                                "units": "gigabytes"
                            }
                        }
                    }
                ]
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_v2_phys_device,
        kind = Envelope::<ProductBundleContainerV2>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-32z5e391.json",
            "data": {
                "name": "Virtual device container",
                "type": "product_bundle_container",
                "fms_entries": [
                    {
                        "name": "generic-x64",
                        "type": "physical_device",
                        "hardware": {
                            "cpu": {
                                "arch": "x64"
                            }
                        }
                    }
                ]
            }
        }
        "#,
        valid = true,
    }
}
