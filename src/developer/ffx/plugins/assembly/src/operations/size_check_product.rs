// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::operations::size_check::PackageSizeInfo;
use crate::operations::size_check::PackageSizeInfos;
use crate::util::read_config;
use anyhow::{format_err, Context, Result};
use assembly_manifest::{AssemblyManifest, BlobfsContents, Image};
use ffx_assembly_args::ProductSizeCheckArgs;
use fuchsia_hash::Hash;
use serde::Serialize;
use std::collections::{HashMap, HashSet};
use std::fs;
use std::str::FromStr;

use super::size_check::PackageBlobSizeInfo;

// The tree structure that needs to be generated for the HTML visualization.
// See "tree_data" in template/D3BlobTreeMap.js.
#[derive(Serialize)]
struct VisualizationRootNode {
    #[serde(rename = "n")]
    name: String,
    children: Vec<VisualizationPackageNode>,
    #[serde(rename = "k")]
    kind: String,
}

#[derive(Serialize)]
struct VisualizationPackageNode {
    #[serde(rename = "n")]
    name: String,
    children: Vec<VisualizationBlobNode>,
    #[serde(rename = "k")]
    kind: String,
}

#[derive(Serialize)]
struct VisualizationBlobNode {
    #[serde(rename = "n")]
    name: String,
    #[serde(rename = "k")]
    kind: String,
    #[serde(rename = "t")]
    uniqueness: String,
    #[serde(rename = "value")]
    proportional_size: u64,
    #[serde(rename = "originalSize")]
    original_size: u64,
    #[serde(rename = "c")]
    share_count: u64,
}

/// Verifies that the product budget is not exceeded.
pub fn verify_product_budgets(args: ProductSizeCheckArgs) -> Result<()> {
    let assembly_manifest: AssemblyManifest = read_config(&args.assembly_manifest)?;
    let blobfs_contents = match extract_blobfs_contents(&assembly_manifest) {
        Some(contents) => contents,
        None => {
            tracing::info!(
                "No blobfs image was found in {}",
                args.assembly_manifest.to_string_lossy()
            );
            return Ok(());
        }
    };
    let max_contents_size = blobfs_contents.maximum_contents_size;
    let package_sizes = calculate_package_sizes(&blobfs_contents)?;
    let total_blobfs_size = calculate_total_blobfs_size(&blobfs_contents)?;
    let contents_fit = match max_contents_size {
        None => true,
        Some(max) => total_blobfs_size <= max,
    };

    if let Some(base_assembly_manifest) = args.base_assembly_manifest {
        let other_assembly_manifest = read_config(&base_assembly_manifest)?;
        let other_blobfs_contents =
            extract_blobfs_contents(&other_assembly_manifest).ok_or(format_err!(
                "Attempted to diff with {} which does not contain a blobfs image",
                base_assembly_manifest.to_string_lossy()
            ))?;
        let other_package_sizes = calculate_package_sizes(&other_blobfs_contents)?;
        print_size_diff(&package_sizes, &other_package_sizes);
    } else if args.verbose || !contents_fit {
        println!("{}", PackageSizeInfos(&package_sizes));
        println!("Total size: {} bytes", total_blobfs_size);
    }

    if max_contents_size.is_none() {
        println!(
            "\nSkipping size checks because maximum_contents_size is not specified for this product."
        );
    }

    if let Some(visualization_dir) = args.visualization_dir {
        fs::create_dir_all(visualization_dir.join("d3_v3"))
            .context("creating d3_v3 directory for visualization")?;
        fs::write(
            visualization_dir.join("d3_v3").join("LICENSE"),
            include_bytes!("../../../../../../../scripts/third_party/d3_v3/LICENSE"),
        )
        .context("creating LICENSE file for visualization")?;
        fs::write(
            visualization_dir.join("d3_v3").join("d3.js"),
            include_bytes!("../../../../../../../scripts/third_party/d3_v3/d3.js"),
        )
        .context("creating d3.js file for visualization")?;
        fs::write(
            visualization_dir.join("D3BlobTreeMap.js"),
            include_bytes!("template/D3BlobTreeMap.js"),
        )
        .context("creating D3BlobTreeMap.js file for visualization")?;
        fs::write(visualization_dir.join("index.html"), include_bytes!("template/index.html"))
            .context("creating index.html file for visualization")?;
        fs::write(
            visualization_dir.join("data.js"),
            format!(
                "var tree_data={}",
                serde_json::to_string(&generate_visualization_tree(&package_sizes))?
            ),
        )
        .context("creating data.js for visualization")?;
        println!("\nWrote visualization to {}", visualization_dir.join("index.html").display());
    }

    if contents_fit {
        Ok(())
    } else {
        Err(format_err!(
            "BlobFS contents size ({}) exceeds max_contents_size ({})",
            total_blobfs_size,
            max_contents_size.unwrap(), // Value is always present when budget is exceeded.
        ))
    }
}

fn generate_visualization_tree(package_sizes: &Vec<PackageSizeInfo>) -> VisualizationRootNode {
    VisualizationRootNode {
        name: "packages".to_string(),
        kind: "p".to_string(),
        children: package_sizes
            .into_iter()
            .map(|package_size| VisualizationPackageNode {
                name: package_size.name.clone(),
                kind: "p".to_string(),
                children: package_size
                    .blobs
                    .iter()
                    .map(|blob| VisualizationBlobNode {
                        name: blob.path_in_package.clone(),
                        kind: "s".to_string(),
                        uniqueness: if blob.share_count == 1 {
                            "unique".to_string()
                        } else {
                            "shared".to_string()
                        },
                        proportional_size: blob.used_space_in_blobfs / blob.share_count,
                        original_size: blob.used_space_in_blobfs,
                        share_count: blob.share_count,
                    })
                    .collect::<Vec<_>>(),
            })
            .collect::<Vec<_>>(),
    }
}

/// Extracts the blobfs contents from the images manifest.
fn extract_blobfs_contents(assembly_manifest: &AssemblyManifest) -> Option<&BlobfsContents> {
    for image in &assembly_manifest.images {
        if let Image::BlobFS { contents, .. } = image {
            return Some(contents);
        }
    }
    None
}

/// Calculates the size of each package in the blobfs image.
/// Result<Vec<PackageSizeInfo>> contains packages in descending order of
/// PackageSizeInfo.used_space_in_blobfs
fn calculate_package_sizes(blobfs_contents: &BlobfsContents) -> Result<Vec<PackageSizeInfo>> {
    let blob_share_count_map = build_blob_share_counts(blobfs_contents);
    let mut package_sizes: Vec<PackageSizeInfo> = blobfs_contents
        .packages
        .base
        .0
        .iter()
        .chain(blobfs_contents.packages.cache.0.iter())
        .map(|p| PackageSizeInfo {
            name: p.name.clone(),
            used_space_in_blobfs: p
                .blobs
                .iter()
                .map(|b| (b.merkle.to_string(), b.used_space_in_blobfs))
                .collect::<HashMap<String, u64>>()
                .values()
                .into_iter()
                .sum::<u64>(),
            proportional_size: p
                .blobs
                .iter()
                .map(|b| {
                    (
                        b.merkle.to_string(),
                        b.used_space_in_blobfs / blob_share_count_map.get(&b.merkle).unwrap(),
                    )
                })
                .collect::<HashMap<String, u64>>()
                .values()
                .into_iter()
                .sum::<u64>(),
            blobs: p
                .blobs
                .iter()
                .map(|b| PackageBlobSizeInfo {
                    merkle: Hash::from_str(&b.merkle).unwrap(),
                    path_in_package: b.path.clone(),
                    used_space_in_blobfs: b.used_space_in_blobfs,
                    share_count: *blob_share_count_map.get(&b.merkle).unwrap(),
                })
                .collect(),
        })
        .collect();
    package_sizes.sort_by(|a, b| b.used_space_in_blobfs.cmp(&a.used_space_in_blobfs));
    Ok(package_sizes)
}

fn build_blob_share_counts(blobfs_contents: &BlobfsContents) -> HashMap<String, u64> {
    let mut blob_share_count_map = HashMap::new();

    blobfs_contents
        .packages
        .base
        .0
        .iter()
        .chain(blobfs_contents.packages.cache.0.iter())
        .flat_map(|p| (&p.blobs).iter().map(|b| b.merkle.to_string()).collect::<HashSet<String>>())
        .for_each(|merkle| {
            blob_share_count_map
                .entry(merkle.to_string())
                .and_modify(|counter| *counter += 1)
                .or_insert(1);
        });
    blob_share_count_map
}

/// Calculates the total size of all the blobs in the blobfs image.
fn calculate_total_blobfs_size(blobfs_contents: &BlobfsContents) -> Result<u64> {
    let merkle_size_map: HashMap<String, u64> = blobfs_contents
        .packages
        .base
        .0
        .iter()
        .chain(blobfs_contents.packages.cache.0.iter())
        .flat_map(|p| p.blobs.iter())
        .map(|b| (b.merkle.to_string(), b.used_space_in_blobfs))
        .collect();

    Ok(merkle_size_map.values().into_iter().sum::<u64>())
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
    use crate::operations::size_check_product::generate_visualization_tree;
    use crate::operations::size_check_product::PackageSizeInfos;
    use crate::operations::size_check_product::{
        build_blob_share_counts, calculate_package_sizes, calculate_total_blobfs_size,
        extract_blobfs_contents, PackageBlobSizeInfo, PackageSizeInfo,
    };
    use crate::util::write_json_file;
    use anyhow::Result;
    use assembly_manifest::{
        AssemblyManifest, BlobfsContents, Image, PackageMetadata, PackageSetMetadata,
        PackagesMetadata,
    };
    use ffx_assembly_args::ProductSizeCheckArgs;
    use fuchsia_hash::Hash;
    use serde_json::json;
    use std::collections::HashMap;
    use std::fs;
    use std::io::Write;
    use std::path::Path;
    use std::str::FromStr;
    use tempfile::TempDir;
    use tempfile::{tempdir, NamedTempFile};

    use super::verify_product_budgets;

    struct TestFs {
        root: TempDir,
    }

    impl TestFs {
        fn new() -> TestFs {
            TestFs { root: TempDir::new().unwrap() }
        }

        fn write(&self, rel_path: &str, value: serde_json::Value) {
            let path = self.root.path().join(rel_path);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            println!("Write {}", path.display());
            write_json_file(&path, &value).unwrap()
        }

        fn path(&self, rel_path: &str) -> std::path::PathBuf {
            self.root.path().join(rel_path)
        }
    }

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
        };
        let mut assembly_manifest = AssemblyManifest {
            images: vec![Image::VBMeta("a/b/c".into()), Image::FVM("x/y/z".into())],
        };
        assert_eq!(extract_blobfs_contents(&assembly_manifest), None);
        assembly_manifest
            .images
            .push(Image::BlobFS { path: "path/to/blob.blk".into(), contents: blobfs_contents });
        let blobfs_contents =
            extract_blobfs_contents(&assembly_manifest).expect("blobfs contents is found");
        assert_eq!(blobfs_contents.maximum_contents_size, Some(1234));
        Ok(())
    }

    #[test]
    fn calculate_package_sizes_test() -> Result<()> {
        let blobfs_contents = create_blobfs_contents();
        let mut package_sizes = calculate_package_sizes(&blobfs_contents)?;

        let mut package_sizes_expected = vec![
            PackageSizeInfo {
                name: "test_cache_package".to_string(),
                used_space_in_blobfs: 120,
                proportional_size: 110,
                blobs: vec![
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                        )?,
                        path_in_package: "bin/defg".to_string(),
                        used_space_in_blobfs: 20,
                        share_count: 2,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568",
                        )?,
                        path_in_package: "lib/ghij".to_string(),
                        used_space_in_blobfs: 60,
                        share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71",
                        )?,
                        path_in_package: "abcd/".to_string(),
                        used_space_in_blobfs: 40,
                        share_count: 1,
                    },
                ],
            },
            PackageSizeInfo {
                name: "test_base_package".to_string(),
                used_space_in_blobfs: 65,
                proportional_size: 55,
                blobs: vec![
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                        )?,
                        path_in_package: "bin/def".to_string(),
                        used_space_in_blobfs: 20,
                        share_count: 2,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567",
                        )?,
                        path_in_package: "lib/ghi".to_string(),
                        used_space_in_blobfs: 30,
                        share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70",
                        )?,
                        path_in_package: "abc/".to_string(),
                        used_space_in_blobfs: 10,
                        share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                        )?,
                        path_in_package: "abc/dupe_file1".to_string(),
                        used_space_in_blobfs: 5,
                        share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                        )?,
                        path_in_package: "abc/dupe_file2".to_string(),
                        used_space_in_blobfs: 5,
                        share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                        )?,
                        path_in_package: "abc/dupe_file3".to_string(),
                        used_space_in_blobfs: 5,
                        share_count: 1,
                    },
                ],
            },
        ];

        package_sizes_expected.iter_mut().for_each(|p| p.blobs.sort_by_key(|b| b.merkle));
        package_sizes.iter_mut().for_each(|p| p.blobs.sort_by_key(|b| b.merkle));

        assert_eq!(package_sizes_expected, package_sizes);

        Ok(())
    }

    #[test]
    fn verify_product_budgets_verbose_output_test() -> Result<()> {
        let blobfs_contents = create_blobfs_contents();
        let package_sizes = calculate_package_sizes(&blobfs_contents)?;
        let expected_output = r#"Package                                                   Size     Proportional Size  Share
test_cache_package                                         120                   110       
   bin/defg                                                 20                    10      2
   lib/ghij                                                 60                    60      1
   abcd/                                                    40                    40      1
test_base_package                                           65                    55       
   bin/def                                                  20                    10      2
   lib/ghi                                                  30                    30      1
   abc/                                                     10                    10      1
   abc/dupe_file1                                            5                     5      1
   abc/dupe_file2*                                           5                     5      1
   abc/dupe_file3*                                           5                     5      1

* indicates that this blob is a duplicate within this package and it therefore does not contribute to the overall package size
"#;
        assert_eq!(
            expected_output.to_string(),
            format!("{}", PackageSizeInfos { 0: &package_sizes })
        );
        Ok(())
    }

    #[test]
    fn build_blob_share_counts_test() -> Result<()> {
        let blobfs_contents = create_blobfs_contents();

        let actual_blob_share_count_map = build_blob_share_counts(&blobfs_contents);
        let expected_blob_share_count_map = HashMap::from([
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70".to_string(), 1u64),
            ("7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581".to_string(), 2u64),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567".to_string(), 1u64),
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71".to_string(), 1u64),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568".to_string(), 1u64),
            ("fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345".to_string(), 1u64),
        ]);
        assert_eq!(expected_blob_share_count_map, actual_blob_share_count_map);

        Ok(())
    }

    #[test]
    fn calculate_total_blobfs_size_test() -> Result<()> {
        let blobfs_contents = create_blobfs_contents();

        let actual_blobfs_size = calculate_total_blobfs_size(&blobfs_contents)?;
        let expected_blobfs_size = 165u64;
        assert_eq!(expected_blobfs_size, actual_blobfs_size);
        Ok(())
    }

    #[test]
    fn verify_product_budgets_with_overflow_test() {
        // Create assembly manifest file
        let test_fs = TestFs::new();
        test_fs.write(
            "assembly_manifest.json",
            json!([
                {
                    "type": "blk",
                    "name": "blob",
                    "path": "obj/build/images/fuchsia/fuchsia/blob.blk",
                    "contents": {
                        "packages": {
                            "base": [
                                {
                                    "name": "",
                                    "manifest": "",
                                    "blobs": [
                                        {
                                            "path": "abc/",
                                            "merkle": "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70",
                                            "used_space_in_blobfs": 10
                                        },
                                        {
                                            "path": "bin/def",
                                            "merkle": "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                                            "used_space_in_blobfs": 20
                                        },
                                        {
                                            "path": "lib/ghi",
                                            "merkle": "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567",
                                            "used_space_in_blobfs": 30
                                        }
                                    ]
                                }
                            ],
                            "cache": [
                                {
                                    "name": "",
                                    "manifest": "",
                                    "blobs": [
                                        {
                                            "path": "abcd/",
                                            "merkle": "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71",
                                            "used_space_in_blobfs": 40
                                        },
                                        {
                                            "path": "bin/defg",
                                            "merkle": "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                                            "used_space_in_blobfs": 20
                                        },
                                        {
                                            "path": "lib/ghij",
                                            "merkle": "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568",
                                            "used_space_in_blobfs": 60
                                        }
                                    ]
                                }
                            ]
                        },
                        "maximum_contents_size": 150,
                        "blobs": [
                            {
                                "merkle": "0088944ae5c00a8d87e76df983bb5ce9c646b4ee9899b898033e51ae088ddf28",
                                "path": "abc/123",
                                "used_space_in_blobfs": 10
                            },
                            {
                                "merkle": "008935139be6a49c6b93124f44d4f0d79a2d5a0f75e951d6a672193a75c83496",
                                "path": "def/234",
                                "used_space_in_blobfs": 50
                            },
                            {
                                "merkle": "01bad8536a7aee498ffd323f53e06232b8a81edd507ac2a95bd0e819c4983138",
                                "path": "ghi/345",
                                "used_space_in_blobfs": 20
                            },
                            {
                                "merkle": "01d321eb2801f8c26a1a8a9c79651f53fef3e3a2ec78b582f48bde50492ce0c4",
                                "path": "jkl/456",
                                "used_space_in_blobfs": 45
                            }
                        ]
                    }
                }
            ]),
        );

        // Create ProductSizeCheckArgs
        let product_size_check_args = ProductSizeCheckArgs {
            assembly_manifest: test_fs.path("assembly_manifest.json"),
            base_assembly_manifest: None,
            verbose: false,
            visualization_dir: None,
        };

        let res = verify_product_budgets(product_size_check_args);
        res.expect_err(
            "Expecting error: BlobFS contents size (125) exceeds max_contents_size (120)",
        );
    }

    #[test]
    fn verify_product_budgets_without_overflow_test() -> Result<()> {
        // Create assembly manifest file
        let test_fs = TestFs::new();
        test_fs.write(
            "assembly_manifest.json",
            json!([
                {
                    "type": "blk",
                    "name": "blob",
                    "path": "obj/build/images/fuchsia/fuchsia/blob.blk",
                    "contents": {
                        "packages": {
                            "base": [
                                {
                                    "name": "",
                                    "manifest": "",
                                    "blobs": [
                                        {
                                            "path": "abc/",
                                            "merkle": "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70",
                                            "used_space_in_blobfs": 10
                                        },
                                        {
                                            "path": "bin/def",
                                            "merkle": "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                                            "used_space_in_blobfs": 20
                                        },
                                        {
                                            "path": "lib/ghi",
                                            "merkle": "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567",
                                            "used_space_in_blobfs": 30
                                        }
                                    ]
                                }
                            ],
                            "cache": [
                                {
                                    "name": "",
                                    "manifest": "",
                                    "blobs": [
                                        {
                                            "path": "abcd/",
                                            "merkle": "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71",
                                            "used_space_in_blobfs": 40
                                        },
                                        {
                                            "path": "bin/defg",
                                            "merkle": "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                                            "used_space_in_blobfs": 20
                                        },
                                        {
                                            "path": "lib/ghij",
                                            "merkle": "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568",
                                            "used_space_in_blobfs": 60
                                        }
                                    ]
                                }
                            ]
                        },
                        "maximum_contents_size": 170,
                        "blobs": [
                            {
                                "merkle": "0088944ae5c00a8d87e76df983bb5ce9c646b4ee9899b898033e51ae088ddf28",
                                "path": "abc/123",
                                "used_space_in_blobfs": 10
                            },
                            {
                                "merkle": "008935139be6a49c6b93124f44d4f0d79a2d5a0f75e951d6a672193a75c83496",
                                "path": "def/234",
                                "used_space_in_blobfs": 50
                            },
                            {
                                "merkle": "01bad8536a7aee498ffd323f53e06232b8a81edd507ac2a95bd0e819c4983138",
                                "path": "ghi/345",
                                "used_space_in_blobfs": 20
                            },
                            {
                                "merkle": "01d321eb2801f8c26a1a8a9c79651f53fef3e3a2ec78b582f48bde50492ce0c4",
                                "path": "jkl/456",
                                "used_space_in_blobfs": 45
                            }
                        ]
                    }
                }
            ]),
        );

        // Create ProductSizeCheckArgs
        let product_size_check_args = ProductSizeCheckArgs {
            assembly_manifest: test_fs.path("assembly_manifest.json"),
            base_assembly_manifest: None,
            verbose: false,
            visualization_dir: None,
        };

        verify_product_budgets(product_size_check_args)
    }

    #[test]
    fn verify_visualization_tree() -> Result<()> {
        let blob1_info = PackageBlobSizeInfo {
            merkle: Hash::from_str(
                "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
            )?,
            path_in_package: "bin/defg".to_string(),
            used_space_in_blobfs: 20,
            share_count: 1,
        };
        let blob2_info = PackageBlobSizeInfo {
            merkle: Hash::from_str(
                "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568",
            )?,
            path_in_package: "lib/ghij".to_string(),
            used_space_in_blobfs: 60,
            share_count: 2,
        };
        let blob3_info = PackageBlobSizeInfo {
            merkle: Hash::from_str(
                "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71",
            )?,
            path_in_package: "abcd/".to_string(),
            used_space_in_blobfs: 40,
            share_count: 1,
        };
        let package_size_infos = vec![
            PackageSizeInfo {
                name: "package1".to_string(),
                used_space_in_blobfs: 80,
                proportional_size: 50,
                blobs: vec![blob1_info.clone(), blob2_info.clone()],
            },
            PackageSizeInfo {
                name: "package2".to_string(),
                used_space_in_blobfs: 100,
                proportional_size: 70,
                blobs: vec![blob2_info.clone(), blob3_info.clone()],
            },
        ];
        let visualization_tree = generate_visualization_tree(&package_size_infos);
        assert_eq!(
            serde_json::to_value(visualization_tree)?,
            json!(
                {
                    "n": "packages",
                    "children": [
                        {
                            "n": "package1",
                            "children": [
                                {
                                    "n": "bin/defg",
                                    "k": "s",
                                    "t": "unique",
                                    "value": 20,
                                    "originalSize": 20,
                                    "c": 1
                                },
                                {
                                    "n": "lib/ghij",
                                    "k": "s",
                                    "t": "shared",
                                    "value": 30,
                                    "originalSize": 60,
                                    "c": 2
                                }
                            ],
                            "k": "p"
                        },
                        {
                            "n": "package2",
                            "children": [
                                {
                                    "n": "lib/ghij",
                                    "k": "s",
                                    "t": "shared",
                                    "value": 30,
                                    "originalSize": 60,
                                    "c": 2
                                },
                                {
                                    "n": "abcd/",
                                    "k": "s",
                                    "t": "unique",
                                    "value": 40,
                                    "originalSize": 40,
                                    "c": 1
                                }
                            ],
                            "k": "p"
                        }
                    ],
                    "k": "p"
                }
            )
        );
        Ok(())
    }

    fn create_blobfs_contents() -> BlobfsContents {
        let dir = tempdir().unwrap();
        // Create base package manifest file
        let base_content = BASE_PACKAGE_MANIFEST.to_string();
        let base_package_manifest_file_name = "base_package_manifest.json".to_string();
        create_package_manifest_file(base_content, &base_package_manifest_file_name, dir.path())
            .unwrap();
        // Create cache package manifest file
        let cache_content = CACHE_PACKAGE_MANIFEST.to_string();
        let cache_package_manifest_file_name = "cache_package_manifest.json".to_string();
        create_package_manifest_file(cache_content, &cache_package_manifest_file_name, dir.path())
            .unwrap();

        let mut blobfs_contents = BlobfsContents::default();
        blobfs_contents.maximum_contents_size = Some(210);
        let merkle_size_map = HashMap::from([
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70".to_string(), 10),
            ("7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581".to_string(), 20),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567".to_string(), 30),
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71".to_string(), 40),
            ("7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff582".to_string(), 50),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568".to_string(), 60),
            ("fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345".to_string(), 5),
        ]);
        blobfs_contents
            .add_base_package(dir.path().join(base_package_manifest_file_name), &merkle_size_map)
            .unwrap();
        blobfs_contents
            .add_cache_package(dir.path().join(cache_package_manifest_file_name), &merkle_size_map)
            .unwrap();
        blobfs_contents
    }

    fn create_package_manifest_file(
        content: String,
        file_name: &String,
        dir_path: &Path,
    ) -> Result<()> {
        let mut package_manifest_file = NamedTempFile::new()?;
        write!(package_manifest_file, "{}", content)?;
        let path = package_manifest_file.into_temp_path();
        path.persist(dir_path.join(file_name))?;
        Ok(())
    }

    static BASE_PACKAGE_MANIFEST: &str = r#"{
            "package": {
                "name": "test_base_package",
                "version": "0"
            },
            "blobs": [
                {
                    "path": "abc/dupe_file1",
                    "merkle": "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                    "size": 8192,
                    "source_path": "../../blobs/fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345"
                },
                {
                    "path": "abc/dupe_file2",
                    "merkle": "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                    "size": 8192,
                    "source_path": "../../blobs/fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345"
                },
                {
                    "path": "abc/dupe_file3",
                    "merkle": "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                    "size": 8192,
                    "source_path": "../../blobs/fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345"
                },
                {
                    "path": "abc/",
                    "merkle": "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70",
                    "size": 2048,
                    "source_path": "../../blobs/eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec78"
                },
                {
                    "path": "bin/def",
                    "merkle": "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                    "size": 188416,
                    "source_path": "../../blobs/7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581"
                },
                {
                    "path": "lib/ghi",
                    "merkle": "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567",
                    "size": 692224,
                    "source_path": "../../blobs/8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567"
                }
            ],
            "version": "1",
            "blob_sources_relative": "file",
            "repository": "fuchsia.com"
        }
        "#;

    static CACHE_PACKAGE_MANIFEST: &str = r#"{
            "package": {
                "name": "test_cache_package",
                "version": "0"
            },
            "blobs": [
                {
                    "path": "abcd/",
                    "merkle": "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71",
                    "size": 1024,
                    "source_path": "../../blobs/eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71"
                },
                {
                    "path": "bin/defg",
                    "merkle": "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
                    "size": 188416,
                    "source_path": "../../blobs/7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581"
                },
                {
                    "path": "lib/ghij",
                    "merkle": "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568",
                    "size": 4096,
                    "source_path": "../../blobs/8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568"
                }
            ],
            "version": "1",
            "blob_sources_relative": "file",
            "repository": "fuchsia.com"
        }
        "#;
}
