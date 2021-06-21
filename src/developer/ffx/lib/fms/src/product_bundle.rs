// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the product_bundle metadata.

use serde::Deserialize;

/// A manifest that describes how to boot an emulator.
#[derive(Debug, Deserialize, PartialEq, Clone)]
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
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct FlashManifest {
    /// A board name used to verify whether the device can be flashed using this
    /// manifest.
    pub hw_revision: String,

    /// A list of product specifications that can be flashed onto the device.
    /// Expect at least one entry.
    pub products: Vec<Product>,
}

/// A set of artifacts necessary to provision a physical or virtual device.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct ImageBundle {
    /// A base URI for accessing artifacts in the bundle.
    pub base_uri: String,

    /// Bundle format: files - a directory layout; tgz - a gzipped tarball.
    pub format: String,
}

/// Manifests describing how to boot the product on a device.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct Manifests {
    /// Optional manifest that describes how to boot an emulator.
    pub emu: Option<EmuManifest>,

    /// Optional manifest that describes how to flash a device.
    pub flash: Option<FlashManifest>,
}

/// A set of artifacts necessary to run a physical or virtual device.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct PackageBundle {
    /// An optional blob repository URI. If omitted, it is assumed to be
    /// <repo_uri>/blobs. If repo_uri refers to a gzipped tarball, ./blobs
    /// directory is expected to be found inside the tarball.
    pub blob_uri: Option<String>,

    /// Repository format: files - a directory layout; tgz - a gzipped tarball.
    pub format: String,

    /// A package repository URI. This may be an archive or a directory.
    pub repo_uri: String,
}

/// A named product specification.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct Product {
    /// A list of partition names and file names corresponding to the
    /// partitions.
    pub bootloader_partitions: Option<Vec<(String, String)>>,

    /// A unique name of this manifest.
    pub name: String,

    /// A list of OEM command and file names corresponding to the command.
    pub oem_files: Option<Vec<(String, String)>>,

    /// A list of partition names and file names corresponding to then
    /// partitions.
    pub partitions: Vec<(String, String)>,
}

/// Description of the data needed to set up (flash) a device.
///
/// This does not include the data "envelope", i.e. it begins within /data in
/// the source json file.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct ProductBundle {
    /// A human readable description of the product bundle.
    pub description: String,

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
    pub metadata: Option<Vec<(String, String)>>,

    /// A list of package bundles. Expect at least one entry.
    pub packages: Vec<PackageBundle>,

    /// A unique name identifying this FMS entry.
    pub name: String,

    /// Always "product_bundle-02" for a ProductBundle. This is valuable for
    /// debugging or when writing this record to a json string.
    pub kind: String,
}
