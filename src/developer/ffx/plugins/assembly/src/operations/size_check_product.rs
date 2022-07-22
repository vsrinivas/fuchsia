// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::operations::size_check::PackageSizeInfo;
use crate::util::read_config;
use anyhow::{format_err, Result};
use assembly_images_manifest::{BlobfsContents, Image, ImagesManifest};
use ffx_assembly_args::ProductSizeCheckArgs;
use fuchsia_hash::Hash;
use std::collections::HashMap;
use std::str::FromStr;

use super::size_check::PackageBlobSizeInfo;

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
            used_space_in_blobfs: p.blobs.iter().map(|b| b.used_space_in_blobfs).sum::<u64>(),
            proportional_size: p
                .blobs
                .iter()
                .map(|b| b.used_space_in_blobfs / blob_share_count_map.get(&b.merkle).unwrap())
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
        .flat_map(|p| p.blobs.iter())
        .for_each(|b| {
            blob_share_count_map
                .entry(b.merkle.to_string())
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

/// Generates verbose output with the size of the contents of the blobfs image broken down by package and blob
/// sorted by package sizes.
fn print_verbose_output(_package_sizes: &Vec<PackageSizeInfo>) {}

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
    use crate::operations::size_check_product::{
        build_blob_share_counts, calculate_package_sizes, calculate_total_blobfs_size,
        extract_blobfs_contents, PackageBlobSizeInfo, PackageSizeInfo,
    };
    use crate::util::write_json_file;
    use anyhow::Result;
    use assembly_images_manifest::{
        BlobfsContents, Image, ImagesManifest, PackageMetadata, PackageSetMetadata,
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
                used_space_in_blobfs: 60,
                proportional_size: 50,
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
                ],
            },
        ];

        package_sizes_expected.iter_mut().for_each(|p| p.blobs.sort_by_key(|b| b.merkle));
        package_sizes.iter_mut().for_each(|p| p.blobs.sort_by_key(|b| b.merkle));

        assert_eq!(package_sizes_expected, package_sizes);

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
        ]);
        assert_eq!(expected_blob_share_count_map, actual_blob_share_count_map);

        Ok(())
    }

    #[test]
    fn calculate_total_blobfs_size_test() -> Result<()> {
        let blobfs_contents = create_blobfs_contents();

        let actual_blobfs_size = calculate_total_blobfs_size(&blobfs_contents)?;
        let expected_blobfs_size = 160u64;
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
        };

        verify_product_budgets(product_size_check_args)
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
