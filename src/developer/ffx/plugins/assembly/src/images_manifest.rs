// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::BasePackage;
use crate::fvm::Fvms;

use anyhow::{Context, Result};
use assembly_util::PathToStringExt;
use serde::{Deserialize, Serialize};
use std::fs::File;
use std::path::{Path, PathBuf};

/// Metadata for a single image file.
#[derive(Serialize, Deserialize)]
pub struct Image {
    name: String,
    path: String,
    #[serde(rename = "type")]
    image_type: String,
}

impl Image {
    fn new(
        name: impl AsRef<str>,
        path: impl AsRef<Path>,
        image_type: impl AsRef<str>,
    ) -> Result<Self> {
        Ok(Self {
            name: name.as_ref().into(),
            path: path.as_ref().path_to_string()?,
            image_type: image_type.as_ref().into(),
        })
    }
}

/// Constructs and writes an images.json manifest to |outdir| which lists
/// metadata for the base package, BlobFS, FVMs, ZBI, and VBMeta.
pub fn construct_images_manifest(
    zbi_path: &PathBuf,
    outdir: &Path,
    base_package: Option<&BasePackage>,
    blobfs_path: Option<&PathBuf>,
    fvms: Option<&Fvms>,
    vbmeta_path: Option<&PathBuf>,
) -> Result<PathBuf> {
    let images = construct_images_metadata(zbi_path, base_package, blobfs_path, fvms, vbmeta_path)?;
    let images_manifest_path = outdir.join("images.json");
    let mut file =
        File::create(&images_manifest_path).context("Failed to create the images manifest file")?;
    serde_json::to_writer_pretty(&mut file, &images)
        .context("Failed to write the images manifest file")?;
    Ok(images_manifest_path)
}

/// Constructs a Vec<Image> which lists metadata for the base package, BlobFS,
/// FVMs, ZBI, and VBMeta.
fn construct_images_metadata(
    zbi_path: &PathBuf,
    base_package: Option<&BasePackage>,
    blobfs_path: Option<&PathBuf>,
    fvms: Option<&Fvms>,
    vbmeta_path: Option<&PathBuf>,
) -> Result<Vec<Image>> {
    let mut images = vec![];
    if let Some(base_package) = base_package {
        images.push(Image::new("base-package", &base_package.path, "far")?);
    }

    if let Some(blobfs_path) = blobfs_path {
        images.push(Image::new("blob", &blobfs_path, "blk")?);
    }

    if let Some(fvms) = fvms {
        images.push(Image::new("storage-full", &fvms.default, "blk")?);
        images.push(Image::new("storage-sparse", &fvms.sparse, "blk")?);
        images.push(Image::new("storage-sparse-blob", &fvms.sparse_blob, "blk")?);
        if let Some(fastboot) = &fvms.fastboot {
            images.push(Image::new("fvm.fastboot", &fastboot, "blk")?);
        }
    }

    images.push(Image::new("zircon-a", &zbi_path, "zbi")?);

    if let Some(vbmeta_path) = vbmeta_path {
        images.push(Image::new("zircon-a", &vbmeta_path, "vbmeta")?);
    }

    Ok(images)
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_hash::Hash;
    use serde_json::json;
    use std::collections::BTreeMap;
    use std::io::BufReader;
    use std::str::FromStr;
    use tempfile::tempdir;

    struct Setup {
        zbi_path: PathBuf,
        base_package: Option<BasePackage>,
        blobfs_path: Option<PathBuf>,
        fvms: Option<Fvms>,
        vbmeta_path: Option<PathBuf>,
        expected: String,
    }

    impl Setup {
        fn new() -> Self {
            Self {
                zbi_path: PathBuf::from("path/to/zbi"),
                base_package: Some(BasePackage {
                    merkle: Hash::from_str(
                        "0000000000000000000000000000000000000000000000000000000000000000",
                    )
                    .unwrap(),
                    contents: BTreeMap::default(),
                    path: PathBuf::from("path/to/base"),
                }),
                blobfs_path: Some(PathBuf::from("path/to/blobfs")),
                fvms: Some(Fvms {
                    default: PathBuf::from("path/to/fvm"),
                    sparse: PathBuf::from("path/to/fvm.sparse"),
                    sparse_blob: PathBuf::from("path/to/fvm.blob.sparse"),
                    fastboot: Some(PathBuf::from("path/to/fvm.fastboot")),
                }),
                vbmeta_path: Some(PathBuf::from("path/to/vbmeta")),
                expected: json!([
                    {
                        "name": "base-package",
                        "path": "path/to/base",
                        "type": "far"
                    },
                    {
                        "name": "blob",
                        "path": "path/to/blobfs",
                        "type": "blk"
                    },
                    {
                        "name": "storage-full",
                        "path": "path/to/fvm",
                        "type": "blk"
                    },
                    {
                        "name": "storage-sparse",
                        "path": "path/to/fvm.sparse",
                        "type": "blk"
                    },
                    {
                        "name": "storage-sparse-blob",
                        "path": "path/to/fvm.blob.sparse",
                        "type": "blk"
                    },
                    {
                        "name": "fvm.fastboot",
                        "path": "path/to/fvm.fastboot",
                        "type": "blk"
                    },
                    {
                        "name": "zircon-a",
                        "path": "path/to/zbi",
                        "type": "zbi"
                    },
                    {
                        "name": "zircon-a",
                        "path": "path/to/vbmeta",
                        "type": "vbmeta"
                    },
                ])
                .to_string(),
            }
        }
    }

    #[test]
    fn test_construct_images_manifest() -> Result<()> {
        let setup = Setup::new();
        let outdir = tempdir()?;
        let images_manifest_path = construct_images_manifest(
            &setup.zbi_path,
            &outdir.path(),
            setup.base_package.as_ref(),
            setup.blobfs_path.as_ref(),
            setup.fvms.as_ref(),
            setup.vbmeta_path.as_ref(),
        )?;

        let images_manifest_file = File::open(&images_manifest_path)?;
        let images: Vec<Image> = serde_json::from_reader(BufReader::new(images_manifest_file))?;
        let images_json = serde_json::to_string(&images)?;
        assert_eq!(setup.expected, images_json);
        Ok(())
    }

    #[test]
    fn test_construct_images_metadata() -> Result<()> {
        let setup = Setup::new();
        let images = construct_images_metadata(
            &setup.zbi_path,
            setup.base_package.as_ref(),
            setup.blobfs_path.as_ref(),
            setup.fvms.as_ref(),
            setup.vbmeta_path.as_ref(),
        )?;

        let images_json = serde_json::to_string(&images)?;
        assert_eq!(setup.expected, images_json);
        Ok(())
    }
}
