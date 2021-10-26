// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::de::{self, Deserializer};
use serde::ser::Serializer;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

/// A manifest containing a list of images produced by the Image Assembler.
///
/// ```
/// use images_manifest::ImagesManifest;
///
/// let manifest = ImagesManifest {
///     images: vec![
///         Image::ZBI("path/to/fuchsia.zbi"),
///         Image::VBMeta("path/to/fuchsia.vbmeta"),
///         Image::FVM("path/to/fvm.blk"),
///         Image::FVMSparse("path/to/fvm.sparse.blk"),
///     ],
/// };
/// println!("{:?}", serde_json::to_value(manifest).unwrap());
/// ```
///
#[derive(Deserialize, Serialize, Debug, Default)]
#[serde(transparent)]
pub struct ImagesManifest {
    /// List of images in the manifest.
    pub images: Vec<Image>,
}

/// A specific Image type.
#[derive(Debug)]
pub enum Image {
    /// Base Package.
    BasePackage(PathBuf),

    /// Zircon Boot Image.
    ZBI(PathBuf),

    /// Verified Boot Metadata.
    VBMeta(PathBuf),

    /// BlobFS image.
    BlobFS(PathBuf),

    /// Fuchsia Volume Manager.
    FVM(PathBuf),

    /// Sparse FVM.
    FVMSparse(PathBuf),

    /// Sparse blobfs-only FVM.
    FVMSparseBlob(PathBuf),

    /// Fastboot FVM.
    FVMFastboot(PathBuf),
}

impl Image {
    /// Get the path of the image on the host.
    pub fn source(&self) -> &PathBuf {
        match self {
            Image::BasePackage(s) => s,
            Image::ZBI(s) => s,
            Image::VBMeta(s) => s,
            Image::BlobFS(s) => s,
            Image::FVM(s) => s,
            Image::FVMSparse(s) => s,
            Image::FVMSparseBlob(s) => s,
            Image::FVMFastboot(s) => s,
        }
    }
}

#[derive(Debug, Serialize)]
struct ImageSerializeHelper<'a> {
    #[serde(rename = "type")]
    partition_type: &'a str,
    name: &'a str,
    path: &'a Path,
}

impl Serialize for Image {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let helper = match self {
            Image::BasePackage(path) => {
                ImageSerializeHelper { partition_type: "far", name: "base-package", path }
            }
            Image::ZBI(path) => {
                ImageSerializeHelper { partition_type: "zbi", name: "zircon-a", path }
            }
            Image::VBMeta(path) => {
                ImageSerializeHelper { partition_type: "vbmeta", name: "zircon-a", path }
            }
            Image::BlobFS(path) => {
                ImageSerializeHelper { partition_type: "blk", name: "blob", path }
            }
            Image::FVM(path) => {
                ImageSerializeHelper { partition_type: "blk", name: "storage-full", path }
            }
            Image::FVMSparse(path) => {
                ImageSerializeHelper { partition_type: "blk", name: "storage-sparse", path }
            }
            Image::FVMSparseBlob(path) => {
                ImageSerializeHelper { partition_type: "blk", name: "storage-sparse-blob", path }
            }
            Image::FVMFastboot(path) => {
                ImageSerializeHelper { partition_type: "blk", name: "fvm.fastboot", path }
            }
        };
        helper.serialize(serializer)
    }
}

#[derive(Debug, Deserialize)]
struct ImageDeserializeHelper {
    #[serde(rename = "type")]
    partition_type: String,
    name: String,
    path: PathBuf,
}

impl<'de> Deserialize<'de> for Image {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let helper = ImageDeserializeHelper::deserialize(deserializer)?;
        match (&helper.partition_type[..], &helper.name[..]) {
            ("far", "base-package") => Ok(Image::BasePackage(helper.path)),
            ("zbi", "zircon-a") => Ok(Image::ZBI(helper.path)),
            ("vbmeta", "zircon-a") => Ok(Image::VBMeta(helper.path)),
            ("blk", "blob") => Ok(Image::BlobFS(helper.path)),
            ("blk", "storage-full") => Ok(Image::FVM(helper.path)),
            ("blk", "storage-sparse") => Ok(Image::FVMSparse(helper.path)),
            ("blk", "storage-sparse-blob") => Ok(Image::FVMSparseBlob(helper.path)),
            ("blk", "fvm.fastboot") => Ok(Image::FVMFastboot(helper.path)),
            (partition_type, name) => Err(de::Error::unknown_variant(
                &format!("({}, {})", partition_type, name),
                &[
                    "(far, base-package)",
                    "(zbi, zircon-a)",
                    "(vbmeta, zircon-a)",
                    "(blk, blob)",
                    "(blk, storage-full)",
                    "(blk, storage-sparse)",
                    "(blk, storage-sparse-blob)",
                    "(blk, fvm.fastboot)",
                ],
            )),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{json, Value};

    #[test]
    fn serialize() {
        let manifest = ImagesManifest {
            images: vec![
                Image::BasePackage("path/to/base.far".into()),
                Image::ZBI("path/to/fuchsia.zbi".into()),
                Image::VBMeta("path/to/fuchsia.vbmeta".into()),
                Image::BlobFS("path/to/blob.blk".into()),
                Image::FVM("path/to/fvm.blk".into()),
                Image::FVMSparse("path/to/fvm.sparse.blk".into()),
                Image::FVMSparseBlob("path/to/fvm.blob.sparse.blk".into()),
                Image::FVMFastboot("path/to/fvm.fastboot.blk".into()),
            ],
        };

        assert_eq!(generate_test_value(), serde_json::to_value(manifest).unwrap());
    }

    #[test]
    fn deserialize() {
        let manifest: ImagesManifest = serde_json::from_value(generate_test_value()).unwrap();
        assert_eq!(manifest.images.len(), 8);

        for image in &manifest.images {
            let (expected, actual) = match image {
                Image::BasePackage(path) => ("path/to/base.far", path),
                Image::ZBI(path) => ("path/to/fuchsia.zbi", path),
                Image::VBMeta(path) => ("path/to/fuchsia.vbmeta", path),
                Image::BlobFS(path) => ("path/to/blob.blk", path),
                Image::FVM(path) => ("path/to/fvm.blk", path),
                Image::FVMSparse(path) => ("path/to/fvm.sparse.blk", path),
                Image::FVMSparseBlob(path) => ("path/to/fvm.blob.sparse.blk", path),
                Image::FVMFastboot(path) => ("path/to/fvm.fastboot.blk", path),
            };
            assert_eq!(&PathBuf::from(expected), actual);
        }
    }

    #[test]
    fn deserialize_invalid() {
        let invalid = json!([
            {
                "type": "far-invalid",
                "name": "base-package",
                "path": "path/to/base.far",
            },
        ]);
        let result: Result<ImagesManifest, _> = serde_json::from_value(invalid);
        assert!(result.unwrap_err().is_data());
    }

    fn generate_test_value() -> Value {
        json!([
            {
                "type": "far",
                "name": "base-package",
                "path": "path/to/base.far",
            },
            {
                "type": "zbi",
                "name": "zircon-a",
                "path": "path/to/fuchsia.zbi",
            },
            {
                "type": "vbmeta",
                "name": "zircon-a",
                "path": "path/to/fuchsia.vbmeta",
            },
            {
                "type": "blk",
                "name": "blob",
                "path": "path/to/blob.blk",
            },
            {
                "type": "blk",
                "name": "storage-full",
                "path": "path/to/fvm.blk",
            },
            {
                "type": "blk",
                "name": "storage-sparse",
                "path": "path/to/fvm.sparse.blk",
            },
            {
                "type": "blk",
                "name": "storage-sparse-blob",
                "path": "path/to/fvm.blob.sparse.blk",
            },
            {
                "type": "blk",
                "name": "fvm.fastboot",
                "path": "path/to/fvm.fastboot.blk",
            },
        ])
    }
}
