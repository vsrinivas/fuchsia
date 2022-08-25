// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the product_bundle metadata.

use {
    crate::{
        common::{ElementType, Envelope},
        json::{schema, JsonObject},
        Metadata,
    },
    anyhow::{bail, Result},
    serde::{Deserialize, Serialize},
};

impl JsonObject for Envelope<ProductBundleV1> {
    fn get_schema() -> &'static str {
        include_str!("../product_bundle-6320eef1.json")
    }

    fn get_referenced_schemata() -> &'static [&'static str] {
        &[schema::COMMON, schema::HARDWARE_V1, schema::EMU_MANIFEST, schema::FLASH_MANIFEST_V1]
    }
}

impl Envelope<ProductBundleV1> {
    pub fn from(data: ProductBundleV1) -> Result<Envelope<ProductBundleV1>> {
        let envelope = Envelope { data, schema_id: Envelope::<ProductBundleV1>::get_schema_id()? };
        Ok(envelope)
    }
}

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

    /// A list of credential files needed for unlocking fastboot devices.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub credentials: Vec<String>,
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
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize, Default)]
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

    /// Does this product require fastboot to be unlocked.
    #[serde(default)]
    pub requires_unlock: bool,
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

/// Product bundle metadata can be an integer, string, or boolean.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(untagged)]
pub enum MetadataValue {
    Integer(u64),
    StringValue(String),
    Boolean(bool),
}

/// Description of the data needed to set up (flash) a device.
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
    pub manifests: Manifests,

    /// A list of key-value pairs describing product dimensions. Tools must not
    /// rely on the presence or absence of certain keys. Tools may display them
    /// to the human user in order to assist them in selecting a desired image
    /// or log them for the sake of analytics. Typical metadata keys are:
    /// build_info_board, build_info_product, is_debug.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub metadata: Option<Vec<(String, MetadataValue)>>,

    /// A list of package bundles. Expect at least one entry.
    pub packages: Vec<PackageBundle>,

    /// A unique name identifying this FMS entry.
    pub name: String,

    /// Always "product_bundle" for a ProductBundle. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub kind: ElementType,
}

/// Versioned product bundle metadata.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(untagged)]
pub enum ProductBundle {
    ProductBundleV1(ProductBundleV1),
}

impl TryFrom<Metadata> for ProductBundle {
    type Error = anyhow::Error;
    #[inline]
    fn try_from(value: Metadata) -> Result<Self> {
        match value {
            Metadata::PhysicalDeviceV1(_) => bail!("No conversion"),
            Metadata::ProductBundleV1(data) => Ok(ProductBundle::ProductBundleV1(data)),
            Metadata::ProductBundleContainerV1(_) => bail!("No conversion"),
            Metadata::ProductBundleContainerV2(_) => bail!("No conversion"),
            Metadata::VirtualDeviceV1(_) => bail!("No conversion"),
        }
    }
}

impl ProductBundle {
    /// Returns ProductBundle entry name.
    pub fn name(&self) -> &str {
        match self {
            Self::ProductBundleV1(pbm) => &pbm.name.as_str(),
        }
    }

    /// Get the list of logical device names.
    pub fn device_refs(&self) -> &Vec<String> {
        match self {
            ProductBundle::ProductBundleV1(pbm) => &pbm.device_refs,
        }
    }

    /// Manifest for the emulator, if present.
    pub fn emu_manifest(&self) -> &Option<EmuManifest> {
        match self {
            ProductBundle::ProductBundleV1(pbm) => &pbm.manifests.emu,
        }
    }
}

/// Validate that the product bundle has the correct format.
pub fn product_bundle_validate(product_bundle: ProductBundleV1) -> Result<()> {
    // TODO(https://fxbug.dev/82728): Add path validation.
    if product_bundle.kind != ElementType::ProductBundle {
        bail!("File type is not ProductBundle");
    }
    if product_bundle.device_refs.is_empty() {
        bail!("At least one 'device_ref' must be supplied");
    }
    if product_bundle.images.is_empty() {
        bail!("At least one entry in 'images' must be supplied");
    }
    for image in product_bundle.images {
        if image.format != "files" && image.format != "tgz" {
            bail!("Only images with format 'files' or 'tgz' are supported");
        }
        if !image.base_uri.starts_with("file:") {
            bail!("Image 'base_uri' paths must start with 'file:'");
        }
    }
    if product_bundle.packages.is_empty() {
        bail!("At least one entry in 'packages' must be supplied");
    }
    for package in product_bundle.packages {
        if package.format != "files" && package.format != "tgz" {
            bail!("Only packages with format 'files' or 'tgz' are supported");
        }
        if let Some(blob_uri) = package.blob_uri {
            if !blob_uri.starts_with("file:") {
                bail!("Package 'blob_uri' paths must start with 'file:'");
            }
        }
        if !package.repo_uri.starts_with("file:") {
            bail!("Package 'repo_uri' paths must start with 'file:'");
        }
    }
    if let Some(flash) = product_bundle.manifests.flash {
        if flash.products.is_empty() {
            bail!("At least one entry in the flash manifest 'products' must be supplied");
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_validation_minimal() {
        let envelope = Envelope::<ProductBundleV1>::new(r#"{
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
        }"#.as_bytes()).unwrap();
        assert!(product_bundle_validate(envelope.data).is_ok());
    }

    #[test]
    fn test_validation_full() {
        let envelope = Envelope::<ProductBundleV1>::new(
            r#"{
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
        }"#
            .as_bytes(),
        )
        .unwrap();
        assert!(product_bundle_validate(envelope.data).is_ok());
    }

    #[test]
    fn test_validation_invalid() {
        let envelope = Envelope::<ProductBundleV1>::new(r#"{
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
            "data": {
                "name": "generic-x64",
                "type": "cc_prebuilt_library",
                "device_refs": ["generic-x64"],
                "images": [{
                    "base_uri": "gs://fuchsia/development/0.20201216.2.1/images/generic-x64.tgz",
                    "format": "tgz"
                }],
                "manifests": {
                },
                "packages": [{
                    "format": "tgz",
                    "repo_uri": "gs://fuchsia/development/0.20201216.2.1/packages/generic-x64.tar.gz"
                }]
            }
        }"#.as_bytes()).unwrap();
        assert!(product_bundle_validate(envelope.data).is_err());
    }

    #[test]
    fn validate_valid() {
        let pb = default_valid_pb();
        assert!(product_bundle_validate(pb).is_ok());
    }

    #[test]
    fn validate_invalid_type() {
        let mut pb = default_valid_pb();
        pb.kind = ElementType::PhysicalDevice;
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_missing_device_ref() {
        let mut pb = default_valid_pb();
        pb.device_refs = vec![];
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_missing_images() {
        let mut pb = default_valid_pb();
        pb.images = vec![];
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_image_invalid_format() {
        let mut pb = default_valid_pb();
        pb.images[0].format = "invalid".into();
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_image_invalid_base_uri() {
        let mut pb = default_valid_pb();
        pb.images[0].base_uri = "gs://path/to/file".into();
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_missing_packages() {
        let mut pb = default_valid_pb();
        pb.packages = vec![];
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_package_invalid_format() {
        let mut pb = default_valid_pb();
        pb.packages[0].format = "invalid".into();
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_package_invalid_blob_uri() {
        let mut pb = default_valid_pb();
        pb.packages[0].blob_uri = Some("gs://path/to/file".into());
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_package_invalid_repo_uri() {
        let mut pb = default_valid_pb();
        pb.packages[0].repo_uri = "gs://path/to/file".into();
        assert!(product_bundle_validate(pb).is_err());
    }

    #[test]
    fn validate_missing_flash_products() {
        let mut pb = default_valid_pb();
        pb.manifests.flash = Some(FlashManifest {
            hw_revision: "board".into(),
            products: vec![],
            credentials: vec![],
        });
        assert!(product_bundle_validate(pb).is_err());
    }

    fn default_valid_pb() -> ProductBundleV1 {
        ProductBundleV1 {
            description: None,
            device_refs: vec!["device".into()],
            images: vec![ImageBundle {
                base_uri: "file://path/to/images".into(),
                format: "files".into(),
            }],
            manifests: Manifests {
                emu: Some(EmuManifest {
                    disk_images: vec!["file://path/to/images".into()],
                    initial_ramdisk: "ramdisk".into(),
                    kernel: "kernel".into(),
                }),
                flash: Some(FlashManifest {
                    hw_revision: "board".into(),
                    products: vec![Product {
                        bootloader_partitions: vec![],
                        name: "product".into(),
                        oem_files: vec![],
                        partitions: vec![],
                        requires_unlock: false,
                    }],
                    credentials: vec![],
                }),
            },
            metadata: None,
            packages: vec![PackageBundle {
                blob_uri: Some("file://path/to/blobs".into()),
                format: "files".into(),
                repo_uri: "file://path/to/repo".into(),
            }],
            name: "default_pb".into(),
            kind: ElementType::ProductBundle,
        }
    }
}
