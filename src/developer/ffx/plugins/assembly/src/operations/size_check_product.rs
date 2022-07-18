// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::operations::size_check::PackageSizeInfo;
use crate::util::read_config;
use anyhow::{format_err, Result};
use assembly_images_manifest::{BlobfsContents, Image, ImagesManifest};
use ffx_assembly_args::ProductSizeCheckArgs;

/// Verifies that the product budget is not exceeded.
pub fn verify_product_budgets(args: ProductSizeCheckArgs) -> Result<()> {
    let images_manifest: ImagesManifest = read_config(&args.assembly_manifest)?;
    let blobfs_contents = match extract_blobfs_contents(&images_manifest) {
        Some(contents) => contents,
        None => {
            tracing::info!(
                "No blobfs image was found in {}",
                args.assembly_manifest.to_string_lossy()
            );
            return Ok(());
        }
    };
    let max_contents_size = blobfs_contents
        .maximum_contents_size
        .ok_or(format_err!("BlobFS max_contents_size is not specified in images manifest"))?;
    let package_sizes = calculate_package_sizes(&blobfs_contents)?;
    let total_blobfs_size = calculate_total_blobfs_size(&blobfs_contents)?;
    let contents_fit = total_blobfs_size <= max_contents_size;

    if let Some(base_assembly_manifest) = args.base_assembly_manifest {
        let other_images_manifest = read_config(&base_assembly_manifest)?;
        let other_blobfs_contents =
            extract_blobfs_contents(&other_images_manifest).ok_or(format_err!(
                "Attempted to diff with {} which does not contain a blobfs image",
                base_assembly_manifest.to_string_lossy()
            ))?;
        let other_package_sizes = calculate_package_sizes(&other_blobfs_contents)?;
        print_size_diff(&package_sizes, &other_package_sizes);
    } else if args.verbose || !contents_fit {
        print_verbose_output(&package_sizes);
    }

    if contents_fit {
        Ok(())
    } else {
        Err(format_err!(
            "BlobFS contents size ({}) exceeds max_contents_size ({})",
            total_blobfs_size,
            max_contents_size
        ))
    }
}

/// Extracts the blobfs contents from the images manifest.
fn extract_blobfs_contents(images_manifest: &ImagesManifest) -> Option<&BlobfsContents> {
    for image in &images_manifest.images {
        if let Image::BlobFS { contents, .. } = image {
            return Some(contents);
        }
    }
    None
}

/// Calculates the size of each package in the blobfs image.
fn calculate_package_sizes(_blobfs_contents: &BlobfsContents) -> Result<Vec<PackageSizeInfo>> {
    unimplemented!()
}

/// Calculates the total size of all the blobs in the blobfs image.
fn calculate_total_blobfs_size(_blobfs_contents: &BlobfsContents) -> Result<u64> {
    unimplemented!()
}

/// Prints the size of the contents of the blobfs image broken down by package and blob
/// sorted by package sizes.
fn print_verbose_output(_package_sizes: &Vec<PackageSizeInfo>) {
    unimplemented!()
}

/// Prints the difference between the contents of two blobfs images broken down by package
/// and blob sorted by the amount of change in size.
fn print_size_diff(
    _package_sizes: &Vec<PackageSizeInfo>,
    _other_package_sizes: &Vec<PackageSizeInfo>,
) {
    unimplemented!()
}

#[cfg(test)]
mod tests {
    use crate::operations::size_check_product::extract_blobfs_contents;
    use anyhow::Result;
    use assembly_images_manifest::{
        BlobfsContents, Image, ImagesManifest, PackageMetadata, PackageSetMetadata,
        PackagesMetadata,
    };

    #[test]
    fn extract_blobfs_contents_test() -> Result<()> {
        let blobfs_contents = BlobfsContents {
            packages: PackagesMetadata {
                base: PackageSetMetadata(vec![PackageMetadata {
                    name: "hello".to_string(),
                    manifest: "path".into(),
                    blobs: Default::default(),
                }]),
                cache: PackageSetMetadata(vec![]),
            },
            maximum_contents_size: Some(1234),
            blobs: Default::default(),
        };
        let mut images_manifest = ImagesManifest {
            images: vec![Image::VBMeta("a/b/c".into()), Image::FVM("x/y/z".into())],
        };
        assert_eq!(extract_blobfs_contents(&images_manifest), None);
        images_manifest
            .images
            .push(Image::BlobFS { path: "path/to/blob.blk".into(), contents: blobfs_contents });
        let blobfs_contents =
            extract_blobfs_contents(&images_manifest).expect("blobfs contents is found");
        assert_eq!(blobfs_contents.maximum_contents_size, Some(1234));
        Ok(())
    }
}
