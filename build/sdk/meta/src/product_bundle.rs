// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the product_bundle metadata.

use crate::common::{ElementType, Envelope};
use crate::json::JsonObject;
use serde::{Deserialize, Serialize};

/// A manifest that describes how to boot an emulator.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct EmuManifest {
    /// A list of one or more disk image paths to FVM images. Each path is
    /// relative to the image bundle base. Expect at least one entry.
    pub disk_images: Vec<String>,

    /// A path to the initial ramdisk, the kernel ZBI. The path is relative to
    /// the image bundle base. Expect at least one character.
    pub initial_ramdisk: String,

    /// A path to the kernel image file. The path is relative to the image
    /// bundle base.  Expect at least one character.
    pub kernel: String,
}

/// A manifest that describes how to flash a device.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct FlashManifest {
    /// A board name used to verify whether the device can be flashed using this
    /// manifest.
    pub hw_revision: String,

    /// A list of product specifications that can be flashed onto the device.
    /// Expect at least one entry.
    pub products: Vec<Product>,
}

/// A set of artifacts necessary to provision a physical or virtual device.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ImageBundle {
    /// A base URI for accessing artifacts in the bundle.
    pub base_uri: String,

    /// Bundle format: files - a directory layout; tgz - a gzipped tarball.
    pub format: String,
}

/// Manifests describing how to boot the product on a device.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Manifests {
    /// Optional manifest that describes how to boot an emulator.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub emu: Option<EmuManifest>,

    /// Optional manifest that describes how to flash a device.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub flash: Option<FlashManifest>,
}

/// A set of artifacts necessary to run a physical or virtual device.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct PackageBundle {
    /// An optional blob repository URI. If omitted, it is assumed to be
    /// <repo_uri>/blobs. If repo_uri refers to a gzipped tarball, ./blobs
    /// directory is expected to be found inside the tarball.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub blob_uri: Option<String>,

    /// Repository format: files - a directory layout; tgz - a gzipped tarball.
    pub format: String,

    /// A package repository URI. This may be an archive or a directory.
    pub repo_uri: String,
}

/// A named product specification.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Product {
    /// A list of partition names and file names corresponding to the
    /// partitions.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub bootloader_partitions: Vec<Partition>,

    /// A unique name of this manifest.
    pub name: String,

    /// A list of OEM command and file names corresponding to the command.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub oem_files: Vec<OemFile>,

    /// A list of partition names and file names corresponding to then
    /// partitions.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub partitions: Vec<Partition>,
}

/// A partition to flash on the target.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct Partition {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub condition: Option<Condition>,
    /// Name of the partition.
    pub name: String,
    /// Path to file on host to upload.
    pub path: String,
}

/// A file to upload and run an OEM command afterwards.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct OemFile {
    /// OEM Command to run after uploading the file.
    pub command: String,
    /// Path to file on host to upload.
    pub path: String,
}

/// A condition that must be true in order to flash a partition.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct Condition {
    /// Variable to check for this flashing condition.
    pub variable: String,
    /// Value of the variable that must match for this condition to be true.
    pub value: String,
}

/// Description of the data needed to set up (flash) a device.
///
/// This does not include the data "envelope", i.e. it begins within /data in
/// the source json file.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductBundleV1 {
    /// A human readable description of the product bundle.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,

    /// A list of physical or virtual device names this product can run on.
    /// Expect at least one entry.
    pub device_refs: Vec<String>,

    /// A list of system image bundles. Expect at least one entry.
    pub images: Vec<ImageBundle>,

    /// Manifests describing how to boot the product on a device.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub manifests: Option<Manifests>,

    /// A list of key-value pairs describing product dimensions. Tools must not
    /// rely on the presence or absence of certain keys. Tools may display them
    /// to the human user in order to assist them in selecting a desired image
    /// or log them for the sake of analytics. Typical metadata keys are:
    /// build_info_board, build_info_product, is_debug.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub metadata: Option<Vec<(String, String)>>,

    /// A list of package bundles. Expect at least one entry.
    pub packages: Vec<PackageBundle>,

    /// A unique name identifying this FMS entry.
    pub name: String,

    /// Always "product_bundle" for a ProductBundle. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub kind: ElementType,
}

impl JsonObject for Envelope<ProductBundleV1> {
    fn get_schema() -> &'static str {
        include_str!("../product_bundle-6320eef1.json")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    test_validation! {
        name = test_validation_minimal,
        kind = Envelope::<ProductBundleV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
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
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_full,
        kind = Envelope::<ProductBundleV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
            "data": {
                "name": "generic-x64",
                "type": "product_bundle",
                "device_refs": ["generic-x64"],
                "images": [{
                    "base_uri": "gs://fuchsia/development/0.20201216.2.1/images/generic-x64.tgz",
                    "format": "tgz"
                }],
                "manifests": {
                    "flash": {
                        "hw_revision": "x64",
                        "products": [{
                            "bootloader_partitions": [],
                            "name": "fuchsia",
                            "oem_files": [],
                            "partitions": [
                                {
                                    "name": "",
                                    "path": "fuchsia.zbi"
                                },
                                {
                                    "name": "",
                                    "path": "zedboot.zbi"
                                },
                                {
                                    "name": "",
                                    "path": "fuchsia.vbmeta"
                                },
                                {
                                    "name": "",
                                    "path": "zedboot.vbmeta"
                                }
                            ]}
                        ]
                    },
                    "emu": {
                        "disk_images": ["fuchsia.zbi"],
                        "initial_ramdisk": "fuchsia.fvm",
                        "kernel": "multiboot.bin"
                    }
                },
                "metadata": [
                    ["build-type", "release"],
                    ["product", "terminal"]
                ],
                "packages": [{
                    "format": "files",
                    "blob_uri": "file:///fuchsia/out/default/amber-files/blobs",
                    "repo_uri": "file:///fuchsia/out/default/amber-files"
                }]
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = Envelope::<ProductBundleV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
            "data": {
                "name": "generic-x64",
                "type": "cc_prebuilt_library",
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
        }
        "#,
        // Incorrect type
        valid = false,
    }
}
