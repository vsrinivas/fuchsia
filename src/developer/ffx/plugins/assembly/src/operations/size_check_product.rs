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
use serde_json::json;
use std::collections::{BTreeSet, HashMap, HashSet};
use std::fs;
use std::str::FromStr;

use super::size_check::PackageBlobSizeInfo;

const TOTAL_BLOBFS_GERRIT_COMPONENT_NAME: &str = "Total BlobFS contents";

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
pub fn verify_product_budgets(args: ProductSizeCheckArgs) -> Result<bool> {
    let assembly_manifest: AssemblyManifest = read_config(&args.assembly_manifest)?;
    let blobfs_contents = match extract_blobfs_contents(&assembly_manifest) {
        Some(contents) => contents,
        None => {
            tracing::info!("No blobfs image was found in {}", args.assembly_manifest);
            return Ok(true);
        }
    };
    let max_contents_size = blobfs_contents.maximum_contents_size;
    let package_sizes = calculate_package_sizes(&blobfs_contents)?;
    let total_blobfs_size = calculate_total_blobfs_size(&blobfs_contents)?;
    let contents_fit = match max_contents_size {
        None => true,
        Some(max) => total_blobfs_size <= max,
    };

    if let Some(size_breakdown_output) = args.size_breakdown_output {
        fs::write(
            size_breakdown_output,
            format!(
                "{}\nTotal size: {} bytes",
                PackageSizeInfos(&package_sizes),
                total_blobfs_size
            ),
        )
        .context("writing size breakdown output")?;
    }

    if let Some(base_assembly_manifest) = args.base_assembly_manifest {
        let other_assembly_manifest = read_config(&base_assembly_manifest)?;
        let other_blobfs_contents =
            extract_blobfs_contents(&other_assembly_manifest).ok_or_else(|| {
                format_err!(
                    "Attempted to diff with {} which does not contain a blobfs image",
                    base_assembly_manifest
                )
            })?;
        let other_package_sizes = calculate_package_sizes(&other_blobfs_contents)?;
        print_size_diff(&package_sizes, &other_package_sizes);
    } else if args.verbose {
        println!("{}", PackageSizeInfos(&package_sizes));
        println!("Total size: {} bytes", total_blobfs_size);
    }

    if let Some(gerrit_output) = args.gerrit_output {
        let max_contents_size = max_contents_size.ok_or_else(|| {
            format_err!("Cannot create gerrit report when max_contents_size is none")
        })?;
        let blobfs_creep_budget = args.blobfs_creep_budget.ok_or_else(|| {
            format_err!("Cannot create gerrit report when blobfs_creep_budget is none")
        })?;
        fs::write(
            gerrit_output,
            serde_json::to_string(&create_gerrit_report(
                total_blobfs_size,
                max_contents_size,
                blobfs_creep_budget,
            ))?,
        )
        .context("writing gerrit report")?;
    }

    if max_contents_size.is_none() && args.verbose {
        println!(
            "Skipping size checks because maximum_contents_size is not specified for this product."
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
        if args.verbose {
            println!("Wrote visualization to {}", visualization_dir.join("index.html"));
        }
    }

    if !contents_fit {
        println!(
            "BlobFS contents size ({}) exceeds max_contents_size ({}).",
            total_blobfs_size,
            max_contents_size.unwrap(), // Value is always present when budget is exceeded.
        );
        if !args.verbose {
            println!("Run with --verbose to view the size breakdown of all packages and blobs, or run `fx size-check` in-tree.");
        }
    }

    Ok(contents_fit)
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
    let blob_share_count_map = build_blob_share_counts(blobfs_contents, false);
    let absolute_blob_share_count_map = build_blob_share_counts(blobfs_contents, true);
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
                    absolute_share_count: *absolute_blob_share_count_map.get(&b.merkle).unwrap(),
                })
                .collect(),
        })
        .collect();
    package_sizes.sort_by(|a, b| b.used_space_in_blobfs.cmp(&a.used_space_in_blobfs));
    Ok(package_sizes)
}

/// is_absolute: false -> Creates a map of merkle to number of packages that share this blob.
/// is_absolute: true -> Creates a map of merkle to number of references to this blob. This doesn't ignore duplicates within a package.
fn build_blob_share_counts(
    blobfs_contents: &BlobfsContents,
    is_absolute: bool,
) -> HashMap<String, u64> {
    let mut blob_share_count_map = HashMap::new();

    blobfs_contents
        .packages
        .base
        .0
        .iter()
        .chain(blobfs_contents.packages.cache.0.iter())
        .flat_map(|p| {
            if is_absolute {
                (&p.blobs).iter().map(|b| b.merkle.to_string()).collect::<Vec<String>>()
            } else {
                (&p.blobs)
                    .iter()
                    .map(|b| b.merkle.to_string())
                    .collect::<HashSet<String>>()
                    .iter()
                    .map(|x| x.to_string())
                    .collect::<Vec<String>>()
            }
        })
        .for_each(|merkle: String| {
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
    package_sizes: &Vec<PackageSizeInfo>,
    other_package_sizes: &Vec<PackageSizeInfo>,
) {
    let mut package_diff_map: HashMap<String, Vec<PackageLevelDiff>> = HashMap::new();
    let common_merkles: HashSet<String> = {
        let package_merkles: HashSet<String> = package_sizes
            .iter()
            .flat_map(|p| p.blobs.iter())
            .map(|b| b.merkle.to_string())
            .collect();
        let other_package_merkles: HashSet<String> = other_package_sizes
            .iter()
            .flat_map(|p| p.blobs.iter())
            .map(|b| b.merkle.to_string())
            .collect();
        package_merkles.intersection(&other_package_merkles).map(|m| m.to_string()).collect()
    };
    let package_sizes_map: HashMap<String, &PackageSizeInfo> =
        package_sizes.iter().map(|p| (p.name.to_string(), p)).collect();
    let other_package_sizes_map: HashMap<String, &PackageSizeInfo> =
        other_package_sizes.iter().map(|p| (p.name.to_string(), p)).collect();
    let all_package_names: HashSet<String> = package_sizes_map
        .keys()
        .into_iter()
        .chain(other_package_sizes_map.keys().into_iter())
        .map(|name| name.to_string())
        .collect();

    let mut product_level_diffs_map: HashMap<String, ProductLevelDiff> = HashMap::new();
    for package_name in all_package_names {
        let package_diff_opt = calculate_package_diff(
            package_sizes_map.get(&package_name),
            other_package_sizes_map.get(&package_name),
            &common_merkles,
        );
        match package_diff_opt {
            Some((package_name, package_level_diffs)) => {
                merge_package_and_product_diffs(
                    package_name.to_string(),
                    &mut product_level_diffs_map,
                    &package_level_diffs,
                );
                package_diff_map.insert(package_name.to_string(), package_level_diffs);
            }
            None => (),
        }
    }

    let product_level_output_lines = generate_product_level_diff_output(product_level_diffs_map);
    product_level_output_lines.iter().for_each(|line| println!("{}", line));

    let package_level_output_lines = generate_package_level_diff_output(package_diff_map);
    package_level_output_lines.iter().for_each(|line| println!("{}", line));

    println!();
    println!("[A] -> indicates that this blob is a new addition in the current image.");
    println!("[D] -> indicates that this blob existed in base image but it is deleted from current image.");
    println!("[M] -> indicates that this is a new blob in current image but it is replacing some existing blob(s) from base image based on path.");
}

fn generate_product_level_diff_output(
    product_level_diffs_map: HashMap<String, ProductLevelDiff>,
) -> Vec<String> {
    let mut product_level_output_lines = vec![];

    let mut product_level_diffs: Vec<&ProductLevelDiff> =
        product_level_diffs_map.values().collect();
    product_level_diffs.sort_by_key(|p| std::cmp::Reverse(p.size_delta as i64));

    product_level_output_lines
        .push(format!("{: <70} {: >15} {: >15}", "Package : Path", "Merkle", "Size Delta"));
    product_level_diffs.iter().for_each(|product_level_diff| {
        product_level_output_lines.push(format!(
            "{: <70} {: >15} {: >+15.2}",
            product_level_diff.package_path,
            &product_level_diff.merkle[..10],
            product_level_diff.size_delta
        ));
    });
    product_level_output_lines.push(format!(
        "{: <86} {: >+15.2}\n",
        "Total Diff",
        product_level_diffs
            .iter()
            .map(|product_level_diff| product_level_diff.size_delta)
            .sum::<f64>()
    ));
    product_level_output_lines
}

fn generate_package_level_diff_output(
    package_diff_map: HashMap<String, Vec<PackageLevelDiff>>,
) -> Vec<String> {
    let mut package_level_output_lines = vec![];

    let mut packages_with_diffs: Vec<&Vec<PackageLevelDiff>> =
        package_diff_map.values().filter(|v| !v.is_empty()).collect();
    packages_with_diffs.sort_by_key(|v| std::cmp::Reverse(v[0].package_size_diff));

    package_level_output_lines.push(format!(
        "{: <56} {: >15} {: >15} {: >+15} {: >20}",
        "Package Name", "Base Merkle", "Current Merkle", "Package Delta", "Proportional Delta"
    ));
    packages_with_diffs.into_iter().for_each(|package_level_diffs| {
        package_level_output_lines.push(format!(
            "{: <56} {: >15} {: >15} {: >+15} {: >20}",
            package_level_diffs[0].package_name,
            "",
            "",
            package_level_diffs[0].package_size_diff,
            ""
        ));
        package_level_diffs.into_iter().for_each(|package_level_diff| {
            package_level_output_lines.push(format!(
                "  {}",
                format!(
                    "{} {: <50} {: >15} {: >15} {: >+15} {: >+20.2}",
                    package_level_diff.modifier_type,
                    package_level_diff.path,
                    package_level_diff
                        .base_merkle_opt
                        .as_ref()
                        .map(|s| String::from(&s[..10]))
                        .unwrap_or("".to_string()),
                    package_level_diff
                        .new_merkle_opt
                        .as_ref()
                        .map(|s| String::from(&s[..10]))
                        .unwrap_or("".to_string()),
                    package_level_diff.package_delta,
                    package_level_diff.proportional_delta
                )
            ))
        })
    });
    package_level_output_lines
}

// Each PackageLevelDiff is compared with the existing entries in product_level_diffs map using merkle as key.
// Information from PackageLevelDiffs are merged into corresponding ProductLevelDiffs.
fn merge_package_and_product_diffs(
    package_name: String,
    product_level_diffs: &mut HashMap<String, ProductLevelDiff>,
    package_level_diffs: &Vec<PackageLevelDiff>,
) {
    package_level_diffs.into_iter().for_each(|package_level_diff| {
        let merkle_key = package_level_diff.new_merkle_opt.as_ref().map_or_else(
            || package_level_diff.base_merkle_opt.as_ref().map(|m| m.to_string()).unwrap(),
            |merkle| merkle.to_string(),
        );
        let product_level_diff_opt = product_level_diffs.get(&merkle_key);
        let existing_sign_opt = product_level_diff_opt.map(|diff| &diff.package_path[..3]);
        let new_sign = package_level_diff.modifier_type.to_string();
        let blob_sign = match existing_sign_opt {
            Some(existing_sign) => {
                if new_sign == "[M]" {
                    new_sign
                } else {
                    existing_sign.to_string()
                }
            }
            None => new_sign,
        };

        product_level_diffs.insert(
            merkle_key.to_string(),
            ProductLevelDiff {
                merkle: merkle_key.to_string(),
                package_path: format!(
                    "{} {}",
                    blob_sign,
                    format!(
                        "{} : {}",
                        package_name.to_string(),
                        package_level_diff.path.to_string()
                    )
                ),
                size_delta: product_level_diff_opt.map(|diff| diff.size_delta).unwrap_or(0.0)
                    + package_level_diff.proportional_delta,
            },
        );
    });
}

#[derive(Debug, PartialEq)]
struct PackageLevelDiff {
    path: String,
    modifier_type: String,
    base_merkle_opt: Option<String>,
    new_merkle_opt: Option<String>,
    package_delta: i32,
    proportional_delta: f64,
    package_name: String,
    package_size_diff: i32,
}

#[derive(Debug, PartialEq)]
struct ProductLevelDiff {
    package_path: String,
    merkle: String,
    size_delta: f64,
}

fn calculate_package_diff(
    current_package_opt: Option<&&PackageSizeInfo>,
    other_package_opt: Option<&&PackageSizeInfo>,
    common_merkles: &HashSet<String>,
) -> Option<(String, Vec<PackageLevelDiff>)> {
    if current_package_opt.is_none() && other_package_opt.is_none() {
        return None;
    }

    let package_name = current_package_opt
        .map_or_else(|| other_package_opt.unwrap().name.to_string(), |p| p.name.to_string());

    let package_size_diff = {
        let current_package_size: i32 =
            i32::try_from(current_package_opt.map(|p| p.used_space_in_blobfs).unwrap_or(0))
                .ok()
                .unwrap();
        let other_package_size: i32 =
            i32::try_from(other_package_opt.map(|p| p.used_space_in_blobfs).unwrap_or(0))
                .ok()
                .unwrap();
        current_package_size - other_package_size
    };

    let mut package_level_diffs: Vec<PackageLevelDiff> = vec![];

    let current_package_blobs_map: HashMap<String, &PackageBlobSizeInfo> = current_package_opt
        .map(|current_package| {
            current_package
                .blobs
                .iter()
                .map(|b| (b.path_in_package.to_string(), b))
                .filter(|b| !common_merkles.contains(&b.1.merkle.to_string()))
                .collect()
        })
        .unwrap_or(HashMap::new());
    let other_package_blobs_map: HashMap<String, &PackageBlobSizeInfo> = other_package_opt
        .map(|other_package| {
            other_package
                .blobs
                .iter()
                .map(|b| (b.path_in_package.to_string(), b))
                .filter(|b| !common_merkles.contains(&b.1.merkle.to_string()))
                .collect()
        })
        .unwrap_or(HashMap::new());

    // Sort paths alphabetically within a package.
    let all_paths: BTreeSet<String> = current_package_blobs_map
        .keys()
        .into_iter()
        .chain(other_package_blobs_map.keys().into_iter())
        .map(|p| p.to_string())
        .collect();

    all_paths.iter().for_each(|path| {
        let current_blob_opt = current_package_blobs_map.get(path);
        let other_blob_opt = other_package_blobs_map.get(path);
        let other_size = i32::try_from(other_blob_opt.map(|b| b.used_space_in_blobfs).unwrap_or(0))
            .ok()
            .unwrap();
        let current_size =
            i32::try_from(current_blob_opt.map(|b| b.used_space_in_blobfs).unwrap_or(0))
                .ok()
                .unwrap();
        let current_proportional_size =
            current_blob_opt.map(|b| calculate_proportional_size(b)).unwrap_or(0.0);
        let other_proportional_size =
            other_blob_opt.map(|b| calculate_proportional_size(b)).unwrap_or(0.0);

        let modifier_type = if current_blob_opt.is_some() && other_blob_opt.is_some() {
            "[M]"
        } else if current_blob_opt.is_some() {
            "[A]"
        } else {
            "[D]"
        };

        if current_proportional_size != other_proportional_size {
            package_level_diffs.push(PackageLevelDiff {
                modifier_type: modifier_type.to_string(),
                path: path.to_string(),
                base_merkle_opt: other_blob_opt.map(|b| b.merkle.to_string()),
                new_merkle_opt: current_blob_opt.map(|b| b.merkle.to_string()),
                package_delta: current_size - other_size,
                proportional_delta: current_proportional_size - other_proportional_size,
                package_name: package_name.to_string(),
                package_size_diff,
            })
        }
    });
    if !package_level_diffs.is_empty() {
        Some((package_name, package_level_diffs))
    } else {
        None
    }
}

fn calculate_proportional_size(blob: &PackageBlobSizeInfo) -> f64 {
    f64::from(blob.used_space_in_blobfs as u32) / f64::from(blob.absolute_share_count as u32)
}

/// Builds a report with the gerrit size format.
fn create_gerrit_report(
    total_blobfs_size: u64,
    max_contents_size: u64,
    blobfs_creep_budget: u64,
) -> serde_json::Value {
    json!({
        TOTAL_BLOBFS_GERRIT_COMPONENT_NAME: total_blobfs_size,
        format!("{}.budget", TOTAL_BLOBFS_GERRIT_COMPONENT_NAME): max_contents_size,
        format!("{}.creepBudget", TOTAL_BLOBFS_GERRIT_COMPONENT_NAME): blobfs_creep_budget
    })
}

#[cfg(test)]
mod tests {
    use crate::operations::size_check_product::{
        build_blob_share_counts, calculate_package_diff, calculate_package_sizes,
        calculate_proportional_size, calculate_total_blobfs_size, create_gerrit_report,
        extract_blobfs_contents, generate_package_level_diff_output,
        generate_product_level_diff_output, generate_visualization_tree,
        merge_package_and_product_diffs, PackageBlobSizeInfo, PackageLevelDiff, PackageSizeInfo,
        PackageSizeInfos, ProductLevelDiff, TOTAL_BLOBFS_GERRIT_COMPONENT_NAME,
    };
    use crate::util::write_json_file;
    use anyhow::Result;
    use assembly_manifest::{
        AssemblyManifest, BlobfsContents, Image, PackageMetadata, PackageSetMetadata,
        PackagesMetadata,
    };
    use camino::{Utf8Path, Utf8PathBuf};
    use ffx_assembly_args::ProductSizeCheckArgs;
    use fuchsia_hash::Hash;
    use serde_json::json;
    use std::collections::{HashMap, HashSet};
    use std::fs;
    use std::io::Write;
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

        fn path(&self, rel_path: &str) -> Utf8PathBuf {
            self.root.path().join(rel_path).try_into().unwrap()
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
                        absolute_share_count: 2,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568",
                        )?,
                        path_in_package: "lib/ghij".to_string(),
                        used_space_in_blobfs: 60,
                        share_count: 1,
                        absolute_share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71",
                        )?,
                        path_in_package: "abcd/".to_string(),
                        used_space_in_blobfs: 40,
                        share_count: 1,
                        absolute_share_count: 1,
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
                        absolute_share_count: 2,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567",
                        )?,
                        path_in_package: "lib/ghi".to_string(),
                        used_space_in_blobfs: 30,
                        share_count: 1,
                        absolute_share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70",
                        )?,
                        path_in_package: "abc/".to_string(),
                        used_space_in_blobfs: 10,
                        share_count: 1,
                        absolute_share_count: 1,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                        )?,
                        path_in_package: "abc/dupe_file1".to_string(),
                        used_space_in_blobfs: 5,
                        share_count: 1,
                        absolute_share_count: 3,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                        )?,
                        path_in_package: "abc/dupe_file2".to_string(),
                        used_space_in_blobfs: 5,
                        share_count: 1,
                        absolute_share_count: 3,
                    },
                    PackageBlobSizeInfo {
                        merkle: Hash::from_str(
                            "fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345",
                        )?,
                        path_in_package: "abc/dupe_file3".to_string(),
                        used_space_in_blobfs: 5,
                        share_count: 1,
                        absolute_share_count: 3,
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
        let expected_output = r#"Package                                                      Merkle        Size     Proportional Size Share
test_cache_package                                                          120                   110      
   bin/defg                                              7ddff81674          20                    10     2
   lib/ghij                                              8cb3466c6e          60                    60     1
   abcd/                                                 eabdb84d26          40                    40     1
test_base_package                                                            65                    55      
   bin/def                                               7ddff81674          20                    10     2
   lib/ghi                                               8cb3466c6e          30                    30     1
   abc/                                                  eabdb84d26          10                    10     1
   abc/dupe_file1                                        fffff84d26           5                     5     1
   abc/dupe_file2*                                       fffff84d26           5                     5     1
   abc/dupe_file3*                                       fffff84d26           5                     5     1

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

        let actual_blob_share_count_map = build_blob_share_counts(&blobfs_contents, false);
        let expected_blob_share_count_map = HashMap::from([
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70".to_string(), 1u64),
            ("7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581".to_string(), 2u64),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567".to_string(), 1u64),
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71".to_string(), 1u64),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568".to_string(), 1u64),
            ("fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345".to_string(), 1u64),
        ]);
        assert_eq!(expected_blob_share_count_map, actual_blob_share_count_map);

        let actual_blob_share_count_map = build_blob_share_counts(&blobfs_contents, true);
        let expected_blob_share_count_map = HashMap::from([
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec70".to_string(), 1u64),
            ("7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581".to_string(), 2u64),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815567".to_string(), 1u64),
            ("eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71".to_string(), 1u64),
            ("8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568".to_string(), 1u64),
            ("fffff84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e594446241112345".to_string(), 3u64),
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
    fn verify_product_budgets_with_overflow_test() -> Result<()> {
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
            gerrit_output: None,
            size_breakdown_output: None,
            blobfs_creep_budget: None,
        };

        assert!(!verify_product_budgets(product_size_check_args)?);
        Ok(())
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
            gerrit_output: None,
            size_breakdown_output: None,
            blobfs_creep_budget: None,
        };

        let res = verify_product_budgets(product_size_check_args);
        assert!(res?);
        Ok(())
    }

    #[test]
    fn verify_visualization_tree_test() -> Result<()> {
        let blob1_info = PackageBlobSizeInfo {
            merkle: Hash::from_str(
                "7ddff816740d5803358dd4478d8437585e8d5c984b4361817d891807a16ff581",
            )?,
            path_in_package: "bin/defg".to_string(),
            used_space_in_blobfs: 20,
            share_count: 1,
            absolute_share_count: 0,
        };
        let blob2_info = PackageBlobSizeInfo {
            merkle: Hash::from_str(
                "8cb3466c6e66592c8decaeaa3e399652fbe71dad5c3df1a5e919743a33815568",
            )?,
            path_in_package: "lib/ghij".to_string(),
            used_space_in_blobfs: 60,
            share_count: 2,
            absolute_share_count: 0,
        };
        let blob3_info = PackageBlobSizeInfo {
            merkle: Hash::from_str(
                "eabdb84d26416c1821fd8972e0d835eedaf7468e5a9ebe01e5944462411aec71",
            )?,
            path_in_package: "abcd/".to_string(),
            used_space_in_blobfs: 40,
            share_count: 1,
            absolute_share_count: 0,
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

    #[test]
    fn gerrit_report_test() {
        let gerrit_report = create_gerrit_report(151, 200, 20);
        assert_eq!(
            gerrit_report,
            json!({
                TOTAL_BLOBFS_GERRIT_COMPONENT_NAME: 151,
                format!("{}.budget", TOTAL_BLOBFS_GERRIT_COMPONENT_NAME): 200,
                format!("{}.creepBudget", TOTAL_BLOBFS_GERRIT_COMPONENT_NAME): 20,
            })
        )
    }

    #[test]
    fn calculate_package_diff_test() -> Result<()> {
        let base_package_size_info = PackageSizeInfo {
            name: "package1".to_string(),
            used_space_in_blobfs: 600,
            proportional_size: 400,
            blobs: vec![
                // A path in base is missing in current
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1",
                    )
                    .unwrap(),
                    path_in_package: "path1".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 1,
                },
                // Merkle of a path in base is modified in current
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2",
                    )
                    .unwrap(),
                    path_in_package: "path2".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 2,
                    absolute_share_count: 3,
                },
                // Merkle of a path in base is being used by more than one path in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3",
                    )
                    .unwrap(),
                    path_in_package: "path3".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 1,
                },
                // Two paths with the same merkle in base are now using two different merkles in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa4",
                    )
                    .unwrap(),
                    path_in_package: "path4".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 2,
                },
                // Two paths with the same merkle in base are now using two different merkles in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa4",
                    )
                    .unwrap(),
                    path_in_package: "path5".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 2,
                },
                // Two paths with different merkles in base are now using same merkle in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa9",
                    )
                    .unwrap(),
                    path_in_package: "path8".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 1,
                },
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa10",
                    )
                    .unwrap(),
                    path_in_package: "path9".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 1,
                },
            ],
        };

        let current_package_size_info = PackageSizeInfo {
            name: "package1".to_string(),
            used_space_in_blobfs: 680,
            proportional_size: 560,
            blobs: vec![
                // A new path is present in the current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa5",
                    )
                    .unwrap(),
                    path_in_package: "path6".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 1,
                },
                // Merkle of a path in base is modified in current
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa6",
                    )
                    .unwrap(),
                    path_in_package: "path2".to_string(),
                    used_space_in_blobfs: 110,
                    share_count: 1,
                    absolute_share_count: 1,
                },
                // Merkle of a path in base is being used by more than one path in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3",
                    )
                    .unwrap(),
                    path_in_package: "path3".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 2,
                },
                // Merkle of a path in base is being used by more than one path in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3",
                    )
                    .unwrap(),
                    path_in_package: "path7".to_string(),
                    used_space_in_blobfs: 100,
                    share_count: 1,
                    absolute_share_count: 2,
                },
                // Two paths with the same merkle in base are now using two different merkles in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa7",
                    )
                    .unwrap(),
                    path_in_package: "path4".to_string(),
                    used_space_in_blobfs: 120,
                    share_count: 1,
                    absolute_share_count: 1,
                },
                // Two paths with the same merkle in base are now using two different merkles in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa8",
                    )
                    .unwrap(),
                    path_in_package: "path5".to_string(),
                    used_space_in_blobfs: 130,
                    share_count: 1,
                    absolute_share_count: 1,
                },
                // Two paths with different merkles in base are now using same merkle in current.
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa11",
                    )
                    .unwrap(),
                    path_in_package: "path8".to_string(),
                    used_space_in_blobfs: 120,
                    share_count: 1,
                    absolute_share_count: 4,
                },
                PackageBlobSizeInfo {
                    merkle: Hash::from_str(
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa11",
                    )
                    .unwrap(),
                    path_in_package: "path9".to_string(),
                    used_space_in_blobfs: 120,
                    share_count: 1,
                    absolute_share_count: 4,
                },
            ],
        };

        let mut common_merkles = HashSet::new();
        common_merkles
            .insert("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3".to_string());
        let package_diff_opt = calculate_package_diff(
            Some(&&current_package_size_info),
            Some(&&base_package_size_info),
            &common_merkles,
        );

        // When a path in base is missing in current, a diff with `-` should be generated.
        let expected_package_level_diff = PackageLevelDiff {
            path: "path1".to_string(),
            modifier_type: "[D]".to_string(),
            base_merkle_opt: Some(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1".to_string(),
            ),
            new_merkle_opt: None,
            package_delta: -100,
            proportional_delta: -100.0,
            package_name: "package1".to_string(),
            package_size_diff: 80,
        };
        let actual_package_level_diff =
            package_diff_opt.as_ref().unwrap().1.iter().find(|p| p.path == "path1");
        assert_eq!(&expected_package_level_diff, actual_package_level_diff.unwrap());

        // When a new path in present in current, a diff with `+` should be generated.
        let expected_package_level_diff = PackageLevelDiff {
            path: "path6".to_string(),
            modifier_type: "[A]".to_string(),
            base_merkle_opt: None,
            new_merkle_opt: Some(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa5".to_string(),
            ),
            package_delta: 100,
            proportional_delta: 100.0,
            package_name: "package1".to_string(),
            package_size_diff: 80,
        };
        let actual_package_level_diff =
            package_diff_opt.as_ref().unwrap().1.iter().find(|p| p.path == "path6");
        assert_eq!(&expected_package_level_diff, actual_package_level_diff.unwrap());

        // When a merkle of a path in base changes in current, a diff with `[M]` should be generated.
        let expected_package_level_diff = PackageLevelDiff {
            path: "path2".to_string(),
            modifier_type: "[M]".to_string(),
            base_merkle_opt: Some(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2".to_string(),
            ),
            new_merkle_opt: Some(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa6".to_string(),
            ),
            package_delta: 10,
            proportional_delta: 76.66666666666666,
            package_name: "package1".to_string(),
            package_size_diff: 80,
        };
        let actual_package_level_diff =
            package_diff_opt.as_ref().unwrap().1.iter().find(|p| p.path == "path2");
        assert_eq!(&expected_package_level_diff, actual_package_level_diff.unwrap());

        // When merkle of a path in base is being used by more than one path in current, no diff is expected
        let expected_package_level_diff = None;
        let actual_package_level_diff =
            package_diff_opt.as_ref().unwrap().1.iter().find(|p| p.path == "path3");
        assert_eq!(expected_package_level_diff, actual_package_level_diff);

        // When two paths with same merkles in base are now using two different merkles in current, two diffs with `[M]` are expected.
        let expected_package_level_diff = vec![
            PackageLevelDiff {
                path: "path4".to_string(),
                modifier_type: "[M]".to_string(),
                base_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa4".to_string(),
                ),
                new_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa7".to_string(),
                ),
                package_delta: 20,
                proportional_delta: 70.0,
                package_name: "package1".to_string(),
                package_size_diff: 80,
            },
            PackageLevelDiff {
                path: "path5".to_string(),
                modifier_type: "[M]".to_string(),
                base_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa4".to_string(),
                ),
                new_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa8".to_string(),
                ),
                package_delta: 30,
                proportional_delta: 80.0,
                package_name: "package1".to_string(),
                package_size_diff: 80,
            },
        ];
        let actual_package_level_diff: Vec<&PackageLevelDiff> = package_diff_opt
            .as_ref()
            .unwrap()
            .1
            .iter()
            .filter(|p| p.path == "path4" || p.path == "path5")
            .collect();
        assert_eq!(
            expected_package_level_diff.iter().collect::<Vec<&PackageLevelDiff>>(),
            actual_package_level_diff
        );

        // When two paths with two different merkles in base are now using the same merkle in current, two diffs with `[M]` are expected.
        let expected_package_level_diff = vec![
            PackageLevelDiff {
                path: "path8".to_string(),
                modifier_type: "[M]".to_string(),
                base_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa9".to_string(),
                ),
                new_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa11".to_string(),
                ),
                package_delta: 20,
                proportional_delta: -70.0,
                package_name: "package1".to_string(),
                package_size_diff: 80,
            },
            PackageLevelDiff {
                path: "path9".to_string(),
                modifier_type: "[M]".to_string(),
                base_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa10".to_string(),
                ),
                new_merkle_opt: Some(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa11".to_string(),
                ),
                package_delta: 20,
                proportional_delta: -70.0,
                package_name: "package1".to_string(),
                package_size_diff: 80,
            },
        ];
        let actual_package_level_diff: Vec<&PackageLevelDiff> = package_diff_opt
            .as_ref()
            .unwrap()
            .1
            .iter()
            .filter(|p| p.path == "path8" || p.path == "path9")
            .collect();
        assert_eq!(
            expected_package_level_diff.iter().collect::<Vec<&PackageLevelDiff>>(),
            actual_package_level_diff
        );

        Ok(())
    }

    #[test]
    fn merge_package_and_product_diffs_test() -> Result<()> {
        // vec[(input_sign, merkle_key, expected_sign)]
        let test_input_and_expectations = vec![
            (
                "[A]".to_string(),
                "1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1".to_string(),
                "[A]".to_string(),
            ),
            (
                "[M]".to_string(),
                "1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1".to_string(),
                "[M]".to_string(),
            ),
            (
                "[D]".to_string(),
                "2aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2".to_string(),
                "[D]".to_string(),
            ),
            (
                "[M]".to_string(),
                "2aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2".to_string(),
                "[M]".to_string(),
            ),
            (
                "[A]".to_string(),
                "3aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3".to_string(),
                "[M]".to_string(),
            ),
            (
                "[M]".to_string(),
                "3aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3".to_string(),
                "[M]".to_string(),
            ),
        ];

        test_input_and_expectations.iter().for_each(|(input_sign, merkle_key, expected_sign)| {
            let mut product_level_diffs: HashMap<String, ProductLevelDiff> =
                create_product_level_diff();
            merge_package_and_product_diffs(
                "package1".to_string(),
                &mut product_level_diffs,
                &vec![PackageLevelDiff {
                    path: "any_path".to_string(),
                    modifier_type: input_sign.to_string(),
                    base_merkle_opt: Some(merkle_key.to_string()),
                    new_merkle_opt: Some(merkle_key.to_string()),
                    package_delta: 10,
                    proportional_delta: 10.0,
                    package_name: "package1".to_string(),
                    package_size_diff: 0,
                }],
            );
            let expected_product_level_diff = &ProductLevelDiff {
                package_path: format!("{} {}", expected_sign, "package1 : any_path".to_string()),
                merkle: merkle_key.to_string(),
                size_delta: 110.0,
            };
            let actual_product_level_diff =
                product_level_diffs.get(&merkle_key.to_string()).unwrap();
            assert_eq!(expected_product_level_diff, actual_product_level_diff);
        });

        Ok(())
    }

    fn create_product_level_diff() -> HashMap<String, ProductLevelDiff> {
        let mut product_level_diffs: HashMap<String, ProductLevelDiff> = HashMap::new();
        product_level_diffs.insert(
            "1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1".to_string(),
            ProductLevelDiff {
                package_path: "[A] package1 : path1".to_string(),
                merkle: "1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1"
                    .to_string(),
                size_delta: 100.0,
            },
        );
        product_level_diffs.insert(
            "2aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2".to_string(),
            ProductLevelDiff {
                package_path: "[D] package1 : path2".to_string(),
                merkle: "2aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2"
                    .to_string(),
                size_delta: 100.0,
            },
        );
        product_level_diffs.insert(
            "3aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3".to_string(),
            ProductLevelDiff {
                package_path: "[M] package1 : path3".to_string(),
                merkle: "3aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3"
                    .to_string(),
                size_delta: 100.0,
            },
        );
        product_level_diffs
    }

    #[test]
    fn generate_package_level_diff_output_test() -> Result<()> {
        let mut package_diff_map: HashMap<String, Vec<PackageLevelDiff>> = HashMap::new();
        package_diff_map.insert(
            "package1".to_string(),
            vec![
                PackageLevelDiff {
                    path: "path1".to_string(),
                    modifier_type: "[M]".to_string(),
                    base_merkle_opt: Some(
                        "1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1"
                            .to_string(),
                    ),
                    new_merkle_opt: Some(
                        "2aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2"
                            .to_string(),
                    ),
                    package_delta: 20,
                    proportional_delta: 20.0,
                    package_name: "package1".to_string(),
                    package_size_diff: 160,
                },
                PackageLevelDiff {
                    path: "path2".to_string(),
                    modifier_type: "[A]".to_string(),
                    base_merkle_opt: None,
                    new_merkle_opt: Some(
                        "3aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3"
                            .to_string(),
                    ),
                    package_delta: 30,
                    proportional_delta: 30.0,
                    package_name: "package1".to_string(),
                    package_size_diff: 160,
                },
                PackageLevelDiff {
                    path: "path3".to_string(),
                    modifier_type: "[D]".to_string(),
                    base_merkle_opt: Some(
                        "4aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa4"
                            .to_string(),
                    ),
                    new_merkle_opt: None,
                    package_delta: 30,
                    proportional_delta: 30.0,
                    package_name: "package1".to_string(),
                    package_size_diff: 160,
                },
            ],
        );
        package_diff_map.insert(
            "package2".to_string(),
            vec![PackageLevelDiff {
                path: "path4".to_string(),
                modifier_type: "[M]".to_string(),
                base_merkle_opt: Some(
                    "5aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa5".to_string(),
                ),
                new_merkle_opt: Some(
                    "6aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa6".to_string(),
                ),
                package_delta: 20,
                proportional_delta: 20.0,
                package_name: "package2".to_string(),
                package_size_diff: 100,
            }],
        );

        let expected_lines: Vec<String> = vec![
            "Package Name                                                 Base Merkle  Current Merkle   Package Delta   Proportional Delta".to_string(),
            "package1                                                                                            +160                     ".to_string(),
            "  [M] path1                                                   1aaaaaaaaa      2aaaaaaaaa             +20               +20.00".to_string(),
            "  [A] path2                                                                   3aaaaaaaaa             +30               +30.00".to_string(),
            "  [D] path3                                                   4aaaaaaaaa                             +30               +30.00".to_string(),
            "package2                                                                                            +100                     ".to_string(),
            "  [M] path4                                                   5aaaaaaaaa      6aaaaaaaaa             +20               +20.00".to_string()

        ];
        let actual_lines = generate_package_level_diff_output(package_diff_map);

        assert_eq!(expected_lines, actual_lines);

        Ok(())
    }

    #[test]
    fn generate_product_level_diff_output_test() -> Result<()> {
        let mut product_level_diffs_map = create_product_level_diff();
        product_level_diffs_map
            .get_mut("1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1")
            .unwrap()
            .size_delta = 130.0;
        product_level_diffs_map
            .get_mut("2aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2")
            .unwrap()
            .size_delta = 110.0;
        product_level_diffs_map
            .get_mut("3aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3")
            .unwrap()
            .size_delta = 120.0;

        let expected_lines: Vec<String> = vec![
            "Package : Path                                                                  Merkle      Size Delta".to_string(),
            "[A] package1 : path1                                                        1aaaaaaaaa         +130.00".to_string(),
            "[M] package1 : path3                                                        3aaaaaaaaa         +120.00".to_string(),
            "[D] package1 : path2                                                        2aaaaaaaaa         +110.00".to_string(),
            "Total Diff                                                                                     +360.00\n".to_string(),
        ];
        let actual_lines = generate_product_level_diff_output(product_level_diffs_map);

        assert_eq!(expected_lines, actual_lines);

        Ok(())
    }

    #[test]
    fn calculate_proportional_size_test() {
        let mut package_blob_size_info = PackageBlobSizeInfo {
            merkle: Hash::from_str(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1",
            )
            .unwrap(),
            path_in_package: "path1".to_string(),
            used_space_in_blobfs: 100,
            share_count: 1,
            absolute_share_count: 1,
        };

        let expected_size = 100.0;
        let actual_size = calculate_proportional_size(&package_blob_size_info);
        assert_eq!(expected_size, actual_size);

        package_blob_size_info.share_count = 2;
        package_blob_size_info.absolute_share_count = 2;
        let expected_size: f64 = 100.0 / 2.0;
        let actual_size = calculate_proportional_size(&package_blob_size_info);
        assert_eq!(expected_size, actual_size);

        package_blob_size_info.share_count = 2;
        package_blob_size_info.absolute_share_count = 3;
        let expected_size: f64 = 100.0 / 3.0;
        let actual_size = calculate_proportional_size(&package_blob_size_info);
        assert_eq!(expected_size, actual_size);
    }

    fn create_blobfs_contents() -> BlobfsContents {
        let tmp = tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Create base package manifest file
        let base_content = BASE_PACKAGE_MANIFEST.to_string();
        let base_package_manifest_file_name = "base_package_manifest.json".to_string();
        create_package_manifest_file(base_content, &base_package_manifest_file_name, dir).unwrap();
        // Create cache package manifest file
        let cache_content = CACHE_PACKAGE_MANIFEST.to_string();
        let cache_package_manifest_file_name = "cache_package_manifest.json".to_string();
        create_package_manifest_file(cache_content, &cache_package_manifest_file_name, dir)
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
            .add_base_package(dir.join(base_package_manifest_file_name), &merkle_size_map)
            .unwrap();
        blobfs_contents
            .add_cache_package(dir.join(cache_package_manifest_file_name), &merkle_size_map)
            .unwrap();
        blobfs_contents
    }

    fn create_package_manifest_file(
        content: String,
        file_name: &String,
        dir_path: &Utf8Path,
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
