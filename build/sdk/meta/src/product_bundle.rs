// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the product_bundle metadata.

use crate::common::Envelope;
use crate::json::JsonObject;
use crate::product_bundle_common::ProductBundleV1;

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
