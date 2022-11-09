// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::blob_json_generator::BlobJsonGenerator;
use crate::operations::size_check::{PackageBlobSizeInfo, PackageSizeInfo};
use crate::util::read_config;
use crate::util::write_json_file;
use anyhow::anyhow;
use anyhow::format_err;
use anyhow::Context;
use anyhow::Result;
use assembly_tool::SdkToolProvider;
use assembly_tool::ToolProvider;
use camino::{Utf8Path, Utf8PathBuf};
use ffx_assembly_args::PackageSizeCheckArgs;
use fuchsia_hash::Hash;
use fuchsia_pkg::PackageManifest;
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::collections::{BTreeMap, HashMap, HashSet};
use url::Url;

/// Blob information. Entry of the "blobs.json" file.
#[derive(Debug, Deserialize, PartialEq)]
pub struct BlobJsonEntry {
    /// Hash of the head for the blob tree.
    pub merkle: Hash,
    /// Size of the content in bytes, once compressed and aligned.
    pub size: u64,
}

/// Root of size checker JSON configuration.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct BudgetConfig {
    /// Apply a size budget to packages.
    #[serde(default)]
    pub package_set_budgets: Vec<PackageSetBudget>,
    /// Apply a size budget to blobs identified by file name.
    /// Matched blobs are excluded from the package accounting.
    /// This takes apart the widely shared resource in their own budget.
    #[serde(default)]
    pub resource_budgets: Vec<ResourceBudget>,
    /// The total amount of bytes equal to the sum of all individual budgets.
    #[serde(default)]
    pub total_budget_bytes: Option<u64>,
}

/// Size budget for a set of packages.
/// Part of JSON configuration.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct PackageSetBudget {
    /// Human readable name of the package set.
    pub name: String,
    /// Number of bytes allotted for the packages of this group.
    pub budget_bytes: u64,
    /// Allowed usage increase allowed for a given commit.
    #[serde(default)]
    pub creep_budget_bytes: u64,
    /// List of paths to `package_manifest.json` files for each package of the set of the group.
    pub packages: Vec<Utf8PathBuf>,
    /// Blobs are de-duplicated by hash across the packages of this set.
    /// This is intended to approximate the process of merging packages.
    #[serde(default)]
    pub merge: bool,
}

/// Size budget for a set of files matched by path.
/// Part of JSON configuration.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct ResourceBudget {
    /// Human readable name of the package set.
    pub name: String,
    /// Number of bytes allotted for the packages of this group.
    pub budget_bytes: u64,
    /// Allowed usage increase allowed for a given commit.
    pub creep_budget_bytes: u64,
    /// File path used to match blobs covered but this budget.
    pub paths: HashSet<String>,
}

/// Intermediate data structure indexed by blob hash used to count how many times a blob is used.
struct BlobSizeAndCount {
    /// Size of the blob content in bytes, once compressed and aligned.
    size: u64,
    /// Number of packages using this blob.
    share_count: u64,
}

#[derive(Clone)]
struct BlobInstance {
    // Hash of the blob merkle root.
    hash: Hash,
    package: Utf8PathBuf,
    path: String,
}

struct BudgetBlobs {
    // TODO(samans): BudgetResult is supposed to represent the result of size checker and should
    // not be used here.
    /// Budget to which one the blobs applies.
    budget: BudgetResult,
    /// List blobs that are charged to a given budget.
    blobs: Vec<BlobInstance>,
}

#[derive(Debug, Serialize, Eq, PartialEq)]
struct BudgetResult {
    /// Human readable name of this budget.
    pub name: String,
    /// Number of bytes allotted to the packages this budget applies to.
    pub budget_bytes: u64,
    /// Allowed usage increase allowed for a given commit.
    pub creep_budget_bytes: u64,
    /// Number of bytes used by the packages this budget applies to.
    pub used_bytes: u64,
    /// Breakdown of storage consumption by package.
    pub package_breakdown: HashMap<Utf8PathBuf, PackageSizeInfo>,
}

/// Verifies that no package budget is exceeded.
pub fn verify_package_budgets(args: PackageSizeCheckArgs) -> Result<()> {
    let sdk_tools = SdkToolProvider::try_new().context("Getting SDK tools")?;
    verify_budgets_with_tools(args, Box::new(sdk_tools))
}

fn verify_budgets_with_tools(
    args: PackageSizeCheckArgs,
    tools: Box<dyn ToolProvider>,
) -> Result<()> {
    let blobfs_builder = BlobJsonGenerator::new(tools, args.blobfs_layout)?;

    // Read the budget configuration file.
    let config: BudgetConfig = read_config(&args.budgets)?;

    // List blobs hashes for each package manifest of each package budget.
    let package_budget_blobs = load_manifests_blobs_match_budgets(&config.package_set_budgets)?;
    let resource_budget_blobs =
        compute_resources_budget_blobs(&config.resource_budgets, &package_budget_blobs);

    // Read blob json file if any, and collect sizes on target.
    let blobs = load_blob_info(&args.blob_sizes)?;
    // Count how many times blobs are used.
    let blob_count_by_hash = count_blobs(&blobs, &package_budget_blobs, &blobfs_builder)?;

    // Find blobs to be charged on the resource budget, and compute each budget usage.
    let mut results =
        compute_budget_results(&resource_budget_blobs, &blob_count_by_hash, &HashSet::new())?;

    // Compute the total size of the packages for each budget, excluding blobs charged on
    // resources budget.
    {
        let resource_hashes: HashSet<&Hash> =
            resource_budget_blobs.iter().flat_map(|b| &b.blobs).map(|b| &b.hash).collect();
        results.append(&mut compute_budget_results(
            &package_budget_blobs,
            &blob_count_by_hash,
            &resource_hashes,
        )?);
    }

    // Write the output result if requested by the command line.
    if let Some(out_path) = &args.gerrit_output {
        write_json_file(&out_path, &to_json_output(&results)?)?;
    }

    // Ensure that the sum of all the budgets equals total budget bytes.
    if let Some(total) = config.total_budget_bytes {
        let mut sum_of_budgets = 0u64;
        for budget in &config.package_set_budgets {
            sum_of_budgets += budget.budget_bytes;
        }
        for budget in &config.resource_budgets {
            sum_of_budgets += budget.budget_bytes;
        }
        if sum_of_budgets != total {
            anyhow::bail!(
                "Sum of budgets doesn't match total budget bytes: sum={}, total={}",
                sum_of_budgets,
                total
            );
        }
    }

    if let Some(verbose_json_output) = args.verbose_json_output {
        let output: HashMap<&str, &BudgetResult> =
            results.iter().map(|v| (v.name.as_str(), v)).collect();
        write_json_file(&verbose_json_output, &output)?;
    }

    // Print a text report for each overrun budget.
    let over_budget = results.iter().filter(|e| e.used_bytes > e.budget_bytes).count();

    if over_budget > 0 {
        println!("FAILED: {} package set(s) over budget", over_budget);
    }
    if args.verbose || over_budget > 0 {
        // Order the results by package set name.
        results.sort_by(|lhs, rhs| lhs.name.cmp(&rhs.name));

        println!("{:<40} {:>10} {:>10} {:>10}", "Package Sets", "Size", "Budget", "Remaining");
        for result in &results {
            // Only print the component usage if it went over budget or verbose output is
            // requested.
            if !args.verbose && result.used_bytes <= result.budget_bytes {
                continue;
            }
            println!(
                "{:<40} {:>10} {:>10} {:>10}",
                result.name,
                result.used_bytes,
                result.budget_bytes,
                result.budget_bytes as i64 - result.used_bytes as i64
            );
            // Only print the package breakdown if verbose output is requested.
            if !args.verbose {
                continue;
            }

            // Order the package breakdown by file name.
            let package_breakdown = result
                .package_breakdown
                .iter()
                .map(|(key, value)| {
                    let name = key.file_name().ok_or_else(|| {
                        format_err!("Can't extract file name from path {:?}", key)
                    })?;
                    Ok((name, value))
                })
                .collect::<Result<BTreeMap<_, _>>>()?;

            for (key, value) in package_breakdown.iter() {
                println!("    {:<36} {:>10}", key, value.proportional_size);
            }
        }
        if let Some(out_path) = &args.gerrit_output {
            println!("Report written to {}", out_path);
        }
    }

    Ok(())
}

/// Reads each mentioned package manifest.
/// Returns pairs of budget and the list of blobs consuming this budget.
fn load_manifests_blobs_match_budgets(budgets: &Vec<PackageSetBudget>) -> Result<Vec<BudgetBlobs>> {
    let mut budget_blobs = Vec::new();
    for budget in budgets.iter() {
        let mut budget_blob = BudgetBlobs {
            budget: BudgetResult {
                name: budget.name.clone(),
                budget_bytes: budget.budget_bytes,
                creep_budget_bytes: budget.creep_budget_bytes,
                used_bytes: 0,
                package_breakdown: HashMap::new(),
            },
            blobs: Vec::new(),
        };

        for package in budget.packages.iter() {
            let manifest = PackageManifest::try_load_from(package)?;
            for manifest_blob in manifest.into_blobs().drain(..) {
                budget_blob.blobs.push(BlobInstance {
                    hash: manifest_blob.merkle,
                    package: package.clone(),
                    path: manifest_blob.path,
                });
            }
        }

        if budget.merge {
            let mut map: HashMap<_, _> = budget_blob
                .blobs
                .drain(..)
                .filter_map(|b| match b.path.as_str() {
                    // Because we are merging the packages into a single package, remove the
                    // meta.fars from each individual input.
                    "meta/" => None,
                    _ => Some((b.hash.clone(), b)),
                })
                .collect();
            budget_blob.blobs = map.drain().map(|(_k, v)| v).collect();

            // Add additional space for the meta.far.
            budget_blob.budget.used_bytes = 32768;
        }

        budget_blobs.push(budget_blob);
    }
    Ok(budget_blobs)
}

/// Reads blob declaration file, and count how many times blobs are used.
/// TODO(fxbug.dev/103906): Pass BlobsJson struct from blobfs.rs as input.
fn load_blob_info(blob_size_paths: &Vec<Utf8PathBuf>) -> Result<Vec<BlobJsonEntry>> {
    let mut result = vec![];
    for blobs_path in blob_size_paths.iter() {
        let mut blobs: Vec<BlobJsonEntry> = read_config(&blobs_path)?;
        result.append(&mut blobs);
    }
    Ok(result)
}

/// Reads blob declaration file, build blobfs for missing blobs, and count how many times blobs are used.
fn index_blobs_by_hash(
    blob_sizes: &Vec<BlobJsonEntry>,
    blob_count_by_hash: &mut HashMap<Hash, BlobSizeAndCount>,
) -> Result<()> {
    for blob_entry in blob_sizes.iter() {
        if let Some(previous) = blob_count_by_hash
            .insert(blob_entry.merkle, BlobSizeAndCount { size: blob_entry.size, share_count: 0 })
        {
            if previous.size != blob_entry.size {
                return Err(anyhow!(
                    "Two blobs with same hash {} but different sizes",
                    blob_entry.merkle
                ));
            }
        }
    }
    Ok(())
}

/// Reads blob declaration file, and count how many times blobs are used.
fn count_blobs(
    blob_sizes: &Vec<BlobJsonEntry>,
    blob_usages: &Vec<BudgetBlobs>,
    blobfs_builder: &BlobJsonGenerator,
) -> Result<HashMap<Hash, BlobSizeAndCount>> {
    // Index blobs by hash.
    let mut blob_count_by_hash: HashMap<Hash, BlobSizeAndCount> = HashMap::new();
    index_blobs_by_hash(blob_sizes, &mut blob_count_by_hash)?;

    // Select packages for which one or more blob is missing.
    let incomplete_packages: Vec<&Utf8Path> = blob_usages
        .iter()
        .flat_map(|budget| &budget.blobs)
        .filter(|blob| !blob_count_by_hash.contains_key(&blob.hash))
        .map(|blob| blob.package.as_path())
        .collect::<HashSet<&Utf8Path>>()
        .drain()
        .collect();

    // If a builder is provided, attempts to build blobfs and complete the blobs database.
    if !incomplete_packages.is_empty() {
        let blobs = blobfs_builder.build(&incomplete_packages).unwrap_or_else(|e| {
            tracing::warn!("Failed to build the blobfs: {}", e);
            Vec::default()
        });
        index_blobs_by_hash(&blobs, &mut blob_count_by_hash)?;
    }

    // Count how many times a blob is shared and report missing blobs.
    for budget_usage in blob_usages.iter() {
        for blob in budget_usage.blobs.iter() {
            match blob_count_by_hash.get_mut(&blob.hash) {
                Some(blob_entry_count) => {
                    blob_entry_count.share_count += 1;
                }
                None => {
                    return Err(anyhow!(
                        "ERROR: Blob not found for budget '{}' package '{}' path '{}' hash '{}'",
                        budget_usage.budget.name,
                        blob.package,
                        blob.path,
                        blob.hash
                    ))
                }
            }
        }
    }

    Ok(blob_count_by_hash)
}

/// Computes the total size of each resource budget.
fn compute_resources_budget_blobs(
    budgets: &Vec<ResourceBudget>,
    package_budget_blobs: &Vec<BudgetBlobs>,
) -> Vec<BudgetBlobs> {
    let mut result = vec![];
    for budget in budgets.iter() {
        // Selects all file hashes matching the path..
        let hashes: HashSet<_> = package_budget_blobs
            .iter()
            .flat_map(|budget| &budget.blobs)
            .filter(|blob| budget.paths.contains(&blob.path))
            .map(|blob| blob.hash.clone())
            .collect();

        // Collect occurrence based on the hash.
        // This finds files with the same content but a different name
        // which should belong to the resource budget.
        result.push(BudgetBlobs {
            budget: BudgetResult {
                name: budget.name.clone(),
                budget_bytes: budget.budget_bytes,
                creep_budget_bytes: budget.creep_budget_bytes,
                used_bytes: 0,
                package_breakdown: HashMap::new(),
            },
            blobs: package_budget_blobs
                .iter()
                .flat_map(|budget| &budget.blobs)
                .filter(|blob| hashes.contains(&blob.hash))
                .cloned()
                .collect(),
        })
    }
    result
}

// Computes the total size of each component taking into account blob sharing.
fn compute_budget_results(
    budget_usages: &Vec<BudgetBlobs>,
    blob_count_by_hash: &HashMap<Hash, BlobSizeAndCount>,
    ignore_hashes: &HashSet<&Hash>,
) -> Result<Vec<BudgetResult>> {
    let mut result = vec![];
    for budget_usage in budget_usages.iter() {
        let mut used_bytes = budget_usage.budget.used_bytes;
        let filtered_blobs = budget_usage
            .blobs
            .iter()
            .filter(|blob| !ignore_hashes.contains(&blob.hash))
            .collect::<Vec<&BlobInstance>>();

        used_bytes += filtered_blobs
            .iter()
            .map(|blob| match blob_count_by_hash.get(&blob.hash) {
                Some(blob_entry_count) => blob_entry_count.size / blob_entry_count.share_count,
                None => 0,
            })
            .sum::<u64>();

        let mut package_breakdown = HashMap::new();
        for blob in filtered_blobs {
            let count = blob_count_by_hash.get(&blob.hash).ok_or(format_err!(
                "Can't find blob {} from package {:?} in map",
                blob.hash,
                blob.package
            ))?;
            let package_result =
                package_breakdown.entry(blob.package.clone()).or_insert(PackageSizeInfo {
                    name: "".to_string(),
                    proportional_size: 0,
                    used_space_in_blobfs: 0,
                    blobs: vec![],
                });
            package_result.proportional_size += count.size / count.share_count;
            package_result.used_space_in_blobfs += count.size;
            package_result.blobs.push(PackageBlobSizeInfo {
                merkle: blob.hash,
                used_space_in_blobfs: count.size,
                share_count: count.share_count,
                absolute_share_count: count.share_count,
                path_in_package: "".to_string(),
            });
        }

        result.push(BudgetResult {
            name: budget_usage.budget.name.clone(),
            used_bytes: used_bytes,
            package_breakdown,
            ..budget_usage.budget
        });
    }
    Ok(result)
}

/// Builds a report with the gerrit size checker format from the computed component size and budget.
fn to_json_output(
    budget_usages: &Vec<BudgetResult>,
) -> Result<BTreeMap<String, serde_json::Value>> {
    // Use an ordered map to ensure the output is readable and stable.
    let mut budget_output = BTreeMap::new();
    for entry in budget_usages.iter() {
        budget_output.insert(entry.name.clone(), json!(entry.used_bytes));
        budget_output.insert(format!("{}.budget", entry.name), json!(entry.budget_bytes));
        budget_output
            .insert(format!("{}.creepBudget", entry.name), json!(entry.creep_budget_bytes));
        let url = Url::parse_with_params(
            "http://go/fuchsia-size-stats/single_component/",
            &[("f", format!("component:in:{}", entry.name))],
        )?;
        budget_output.insert(format!("{}.owner", entry.name), json!(url.as_str()));
    }
    Ok(budget_output)
}

#[cfg(test)]
mod tests {
    use crate::operations::size_check_package::{
        compute_budget_results, verify_budgets_with_tools, BlobInstance, BlobSizeAndCount,
        BudgetBlobs, BudgetConfig, BudgetResult, PackageBlobSizeInfo, PackageSizeInfo,
    };
    use crate::util::read_config;
    use crate::util::write_json_file;
    use anyhow::Result;
    use assembly_images_config::BlobFSLayout;
    use assembly_tool::testing::FakeToolProvider;
    use camino::Utf8PathBuf;
    use errors::ResultExt;
    use ffx_assembly_args::PackageSizeCheckArgs;
    use fuchsia_hash::Hash;
    use serde_json::json;
    use std::collections::{HashMap, HashSet};
    use std::fs;
    use std::path::Path;
    use std::str::FromStr;
    use tempfile::TempDir;

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

        fn assert_eq(&self, rel_path: &str, expected: serde_json::Value) {
            let path = self.root.path().join(rel_path);
            let actual: serde_json::Value = read_config(&path).unwrap();
            assert_eq!(actual, expected);
        }

        fn path(&self, rel_path: &str) -> Utf8PathBuf {
            self.root.path().join(rel_path).try_into().unwrap()
        }
    }

    fn assert_failed<E>(err: Result<(), E>, prefix: &str)
    where
        E: std::fmt::Display,
    {
        match err {
            Ok(_) => panic!("Unexpected success, where a failure was expected."),
            Err(e) => assert!(
                e.to_string().starts_with(prefix),
                "Unexpected error message:\n\t{:#}\ndoes not start with:\n\t{}",
                e,
                prefix
            ),
        }
    }

    #[test]
    fn default_merge_and_creep() {
        let budgets: BudgetConfig = serde_json::from_value(json!({
        "package_set_budgets":[
            {
                "name": "budget_name",
                "budget_bytes": 10,
                "packages": [],
            },
        ]}))
        .unwrap();
        assert_eq!(budgets.package_set_budgets.len(), 1);
        let package_set_budget = &budgets.package_set_budgets[0];
        assert_eq!(package_set_budget.merge, false);
        assert_eq!(package_set_budget.creep_budget_bytes, 0);
    }

    #[test]
    fn fails_because_of_missing_blobs_file() {
        let test_fs = TestFs::new();
        test_fs.write("size_budgets.json", json!({}));
        let err = verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: None,
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        );
        assert_failed(err, "Unable to open file:");
    }

    #[test]
    fn fails_because_of_missing_budget_file() {
        let test_fs = TestFs::new();
        test_fs.write("blobs.json", json!([]));
        let err = verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: None,
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        );
        assert_eq!(err.exit_code(), 1);
        assert_failed(err, "Unable to open file:");
    }

    #[test]
    fn fails_because_exceeds_maximum_budget() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({
            "package_set_budgets":[
                {
                    "name": "Software Delivery",
                    "budget_bytes": 27,
                    "creep_budget_bytes": 2i32,
                    "merge":false,
                    "packages": [],
                },
                {
                    "name": "Component Framework",
                    "budget_bytes": 51i32,
                    "creep_budget_bytes": 2i32,
                    "merge":false,
                    "packages": [],
                }
            ],
            "resource_budgets":[
                {
                    "name": "Misc",
                    "budget_bytes": 23,
                    "creep_budget_bytes": 2i32,
                    "paths": [],
                },
            ],
            "total_budget_bytes": 100}),
        );
        test_fs.write("blobs.json", json!([]));
        let err = verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: None,
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        );
        assert_eq!(err.exit_code(), 1);
        assert_failed(err, "Sum of budgets doesn't match total budget bytes:");
    }

    #[test]
    fn fails_because_falls_short_of_maximum_budget() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({
            "package_set_budgets":[
                {
                    "name": "Software Delivery",
                    "budget_bytes": 27,
                    "creep_budget_bytes": 2i32,
                    "merge":false,
                    "packages": [],
                },
                {
                    "name": "Component Framework",
                    "budget_bytes": 51i32,
                    "creep_budget_bytes": 2i32,
                    "merge":false,
                    "packages": [],
                }
            ],
            "resource_budgets":[
                {
                    "name": "Misc",
                    "budget_bytes": 20,
                    "creep_budget_bytes": 2i32,
                    "paths": [],
                },
            ],
            "total_budget_bytes": 100}),
        );
        test_fs.write("blobs.json", json!([]));
        let err = verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: None,
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        );
        assert_eq!(err.exit_code(), 1);
        assert_failed(err, "Sum of budgets doesn't match total budget bytes:");
    }

    #[test]
    fn succeeds_because_equals_maximum_budget() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({
            "package_set_budgets":[
                {
                    "name": "Software Delivery",
                    "budget_bytes": 27,
                    "creep_budget_bytes": 2i32,
                    "merge":false,
                    "packages": [],
                },
                {
                    "name": "Component Framework",
                    "budget_bytes": 51i32,
                    "creep_budget_bytes": 2i32,
                    "merge":false,
                    "packages": [],
                }
            ],
            "resource_budgets":[
                {
                    "name": "Misc",
                    "budget_bytes": 23,
                    "creep_budget_bytes": 2i32,
                    "paths": [],
                },
            ],
            "total_budget_bytes": 101}),
        );
        test_fs.write("blobs.json", json!([]));
        verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: None,
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        )
        .unwrap();
    }

    #[test]
    fn duplicate_merkle_in_blobs_file_with_different_sizes_causes_failure() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({"package_set_budgets":[{
                "name": "Software Delivery",
                "budget_bytes": 1i32,
                "creep_budget_bytes": 2i32,
                "merge":false,
                "packages": [],
            }]}),
        );

        test_fs.write(
            "blobs.json",
            json!([{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 8i32
            },{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 16i32
            }]),
        );
        let res = verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: None,
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        );
        assert_failed(res, "Two blobs with same hash 0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51 but different sizes");
    }

    #[test]
    fn duplicate_merkle_in_blobs_with_same_size_are_fine() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({"package_set_budgets" :[{
                "name": "Software Deliver",
                "budget_bytes": 1i32,
                "creep_budget_bytes": 1i32,
                "merge": false,
                "packages": [],
            }]}),
        );

        test_fs.write(
            "blobs.json",
            json!([{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 16i32
            },{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 16i32
            }]),
        );
        verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: None,
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        )
        .unwrap();
    }

    #[test]
    fn two_blobs_with_one_shared_fails_over_budget() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({"package_set_budgets":[{
                "name": "Software Deliver",
                "budget_bytes": 1i32,
                "creep_budget_bytes": 2i32,
                "merge": true,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                    test_fs.path("obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json"),
                ]
            }]}),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json",
            json!({
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "ignored",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 16i32
                },{
                    "source_path": "second_blob_used_by_two_packages_of_the_component",
                    "path": "ignored",
                    "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                    "size": 64i32
                }]
            }),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json",
            json!({
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "second_blob_used_by_two_packages_of_the_component",
                    "path": "ignored",
                    "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                    "size": 64i32
                }]
            }),
        );

        test_fs.write(
            "blobs.json",
            json!([{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 8i32
            },{
                "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                "size": 32i32
            },{
                "merkle": "01ecd6256f89243e1f0f7d7022cc2e8eb059b06c941d334d9ffb108478749646",
                "size": 164i32
            }]),
        );
        let res = verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: Some(test_fs.path("output.json")),
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        );

        test_fs.assert_eq(
            "output.json",
            json!({
                "Software Deliver": 32808i32,
                "Software Deliver.budget": 1i32,
                "Software Deliver.creepBudget": 2i32,
                "Software Deliver.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"
            }),
        );
        // Exceeding budgets does not fail the build.
        assert!(matches!(res, Ok(())));
    }

    #[test]
    fn blob_size_are_summed_test() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({"package_set_budgets":[{
                "name": "Software Deliver",
                "creep_budget_bytes": 2i32,
                "budget_bytes": 7497932i32,
                "merge": false,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                ]
            }]}),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json",
            json!({
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "first_blob",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 1i32
                },{
                    "source_path": "second_blob",
                    "path": "second_blob",
                    "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                    "size": 2i32
                }]
            }),
        );
        test_fs.write(
            "blobs.json",
            json!([{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 8i32
            },{
                "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                "size": 32i32
            },{
                "merkle": "01ecd6256f89243e1f0f7d7022cc2e8eb059b06c941d334d9ffb108478749646",
                "size": 128i32
            }]),
        );
        verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: Some(test_fs.path("output.json")),
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        )
        .unwrap();

        test_fs.assert_eq(
            "output.json",
            json!({
                "Software Deliver": 40i32,
                "Software Deliver.budget": 7497932i32,
                "Software Deliver.creepBudget": 2i32,
                "Software Deliver.owner":
                "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"}),
        );
    }

    #[test]
    fn blob_shared_by_two_budgets_test() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({"package_set_budgets":[{
            "name": "Software Deliver",
                "creep_budget_bytes": 1i32,
                "budget_bytes": 7497932i32,
                "merge": false,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                    test_fs.path("obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json"),
                ]
            },{
                "name": "Connectivity",
                "creep_budget_bytes": 1i32,
                "budget_bytes": 10884219,
                "merge": false,
                "packages": [
                    test_fs.path( "obj/src/connectivity/bluetooth/core/bt-gap/bt-gap/package_manifest.json"),]
            }]}),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json",
            json!({
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "ignored",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 4i32
                }]
            }),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json",
            json!({
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "ignored",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 8i32
                }]
            }),
        );
        test_fs.write(
            "obj/src/connectivity/bluetooth/core/bt-gap/bt-gap/package_manifest.json",
            json!({
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "ignored",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 16i32
                }]
            }),
        );
        test_fs.write(
            "blobs.json",
            json!([{
              "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
              "size": 159i32
            }]),
        );
        verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: Some(test_fs.path("output.json")),
                verbose: false,
                verbose_json_output: Some(test_fs.path("verbose-output.json")),
            },
            Box::new(FakeToolProvider::default()),
        )
        .unwrap();

        test_fs.assert_eq(
            "output.json",
            json!({
                "Connectivity": 53i32,
                "Connectivity.creepBudget": 1i32,
                "Connectivity.budget": 10884219i32,
                "Connectivity.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3AConnectivity",
                "Software Deliver": 106i32,
                "Software Deliver.creepBudget": 1i32,
                "Software Deliver.budget": 7497932i32,
                "Software Deliver.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"
            }),
        );
        test_fs.assert_eq(
            "verbose-output.json",
            json!({
                "Software Deliver": {
                  "name": "Software Deliver",
                  "budget_bytes": 7497932,
                  "creep_budget_bytes": 1,
                  "used_bytes": 106,
                  "package_breakdown": {
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json").to_string(): {
                      "proportional_size": 53,
                      "used_space_in_blobfs": 159,
                      "name": "",
                      "blobs": [
                        {
                            "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                            "path_in_package": "",
                            "used_space_in_blobfs": 159,
                            "share_count": 3,
                            "absolute_share_count": 3,
                        }
                      ]
                    },
                    test_fs.path("obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json").to_string(): {
                      "proportional_size": 53,
                      "used_space_in_blobfs": 159,
                      "name": "",
                      "blobs": [
                        {
                            "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                            "path_in_package": "",
                            "used_space_in_blobfs": 159,
                            "share_count": 3,
                            "absolute_share_count": 3,
                        }
                      ]
                    }
                  }
                },
                "Connectivity": {
                  "name": "Connectivity",
                  "budget_bytes": 10884219,
                  "creep_budget_bytes": 1,
                  "used_bytes": 53,
                  "package_breakdown": {
                    test_fs.path("obj/src/connectivity/bluetooth/core/bt-gap/bt-gap/package_manifest.json").to_string(): {
                      "proportional_size": 53,
                      "used_space_in_blobfs": 159,
                      "name": "",
                      "blobs": [
                        {
                            "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                            "path_in_package": "",
                            "used_space_in_blobfs": 159,
                            "share_count": 3,
                            "absolute_share_count": 3,
                        }
                      ]
                    }
                  }
                }
              }
              )
        );
    }

    #[test]
    fn blob_hash_not_found_test() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({"package_set_budgets":[{
                "name": "Connectivity",
                "creep_budget_bytes": 1i32,
                "budget_bytes": 7497932i32,
                "merge": false,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                ]
            }]}),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json",
            json!({
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "not found",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 4i32
                }]
            }),
        );

        test_fs.write("blobs.json", json!([]));
        let err = verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: Some(test_fs.path("output.json")),
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        );

        assert_failed(err, "ERROR: Blob not found for budget 'Connectivity' package")
    }

    #[test]
    fn blobs_matched_by_resource_budget() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({
                "resource_budgets": [{
                    "name": "libs",
                    "creep_budget_bytes": 1i32,
                    "budget_bytes": 32i32,
                    "paths": [
                        "a/lib_a"
                    ]
                }],
                "package_set_budgets":[{
                    "name": "Software Deliver",
                    "creep_budget_bytes": 2i32,
                    "budget_bytes": 64i32,
                    "merge": false,
                    "packages": [
                        test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                        test_fs.path("obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json"),
                    ]
                }]
            }),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json",
            json!({
                "version": "1",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "a/lib_a",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 4i32
                }]
            }),
        );
        test_fs.write(
            "obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json",
            json!({
                "version": "1",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": "first_blob",
                    "path": "b/lib_a",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 8i32
                }]
            }),
        );
        test_fs.write(
            "blobs.json",
            json!([{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 9i32
            }]),
        );
        verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::Compact,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs.json")].to_vec(),
                gerrit_output: Some(test_fs.path("output.json")),
                verbose: false,
                verbose_json_output: None,
            },
            Box::new(FakeToolProvider::default()),
        )
        .unwrap();

        test_fs.assert_eq(
            "output.json",
            json!({
                "libs": 8i32, // Rounding error from 9 to 8 is expected.
                "libs.creepBudget": 1i32,
                "libs.budget": 32i32,
                "libs.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3Alibs",
                "Software Deliver": 0i32,
                "Software Deliver.creepBudget": 2i32,
                "Software Deliver.budget": 64i32,
                "Software Deliver.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"
            }),
        );
    }

    #[test]
    fn generating_blobfs_for_a_missing_file() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!({
                "package_set_budgets":[{
                    "name": "Software Deliver",
                    "creep_budget_bytes": 2i32,
                    "budget_bytes": 256i32,
                    "merge": false,
                    "packages": [
                        test_fs.path("obj/src/my_program/package_manifest.json"),
                    ]
                }]
            }),
        );
        test_fs.write(
            "obj/src/my_program/package_manifest.json",
            json!({
                "version": "1",
                "package": {
                    "name": "pkg-cache",
                    "version": "0"
                },
                "blobs" : [{
                    "source_path": test_fs.path("first.txt"),
                    "path": "first",
                    "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                    "size": 8i32
                }, {
                    "source_path": test_fs.path("second.txt"),
                    "path": "second",
                    "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2a",
                    "size": 16i32
                }]
            }),
        );
        test_fs.write("first.txt", json!("some text content"));
        test_fs.write("second.txt", json!("some other text content"));
        test_fs.write(
            "blobs1.json",
            json!([{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 37i32
            }]),
        );
        let tool_provider =
            Box::new(FakeToolProvider::new_with_side_effect(|_name: &str, args: &[String]| {
                assert_eq!(args[0], "--json-output");
                write_json_file(
                    Path::new(&args[1]),
                    &json!([{
                      "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2a",
                      "size": 73
                    }]),
                )
                .unwrap();
            }));
        verify_budgets_with_tools(
            PackageSizeCheckArgs {
                blobfs_layout: BlobFSLayout::DeprecatedPadded,
                budgets: test_fs.path("size_budgets.json"),
                blob_sizes: [test_fs.path("blobs1.json")].to_vec(),
                gerrit_output: Some(test_fs.path("output.json")),
                verbose: false,
                verbose_json_output: None,
            },
            tool_provider,
        )
        .unwrap();

        test_fs.assert_eq(
            "output.json",
            json!({
                "Software Deliver": 110i32,
                "Software Deliver.creepBudget": 2i32,
                "Software Deliver.budget": 256i32,
                "Software Deliver.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"
            }),
        );
    }

    #[test]
    fn test_package_breakdown() -> Result<()> {
        let blob1_hash: Hash =
            Hash::from_str("0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51")
                .unwrap();
        let blob2_hash: Hash =
            Hash::from_str("b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2a")
                .unwrap();
        let blob3_hash: Hash =
            Hash::from_str("b61ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2a")
                .unwrap();
        let blob1_path: &str = "/a/x/blob1";
        let blob2_path: &str = "/a/x/blob2";
        let blob3_path: &str = "/a/x/blob3";
        let package1_path = Utf8PathBuf::from("x/a/s/package1");
        let package2_path = Utf8PathBuf::from("x/a/s/package2");
        let package3_path = Utf8PathBuf::from("x/a/s/package3");

        let resource_budget_blobs: Vec<BudgetBlobs> = vec![
            BudgetBlobs {
                budget: BudgetResult {
                    name: "Component1".to_string(),
                    budget_bytes: 123,
                    creep_budget_bytes: 3245,
                    used_bytes: 0,
                    package_breakdown: HashMap::new(),
                },
                blobs: vec![
                    BlobInstance {
                        hash: blob1_hash.clone(),
                        package: package1_path.clone(),
                        path: blob1_path.to_string(),
                    },
                    BlobInstance {
                        hash: blob3_hash.clone(),
                        package: package1_path.clone(),
                        path: blob3_path.to_string(),
                    },
                    BlobInstance {
                        hash: blob2_hash.clone(),
                        package: package2_path.clone(),
                        path: blob2_path.to_string(),
                    },
                    BlobInstance {
                        hash: blob1_hash.clone(),
                        package: package2_path.clone(),
                        path: blob1_path.to_string(),
                    },
                ],
            },
            BudgetBlobs {
                budget: BudgetResult {
                    name: "Component2".to_string(),
                    budget_bytes: 456,
                    creep_budget_bytes: 111,
                    used_bytes: 6,
                    package_breakdown: HashMap::new(),
                },
                blobs: vec![BlobInstance {
                    hash: blob2_hash.clone(),
                    package: package3_path.clone(),
                    path: blob2_path.to_string(),
                }],
            },
        ];
        let blob_count_by_hash: HashMap<Hash, BlobSizeAndCount> = HashMap::from([
            (blob1_hash.clone(), BlobSizeAndCount { size: 90, share_count: 2 }),
            (blob2_hash.clone(), BlobSizeAndCount { size: 50, share_count: 2 }),
            (blob3_hash.clone(), BlobSizeAndCount { size: 1000, share_count: 1 }),
        ]);
        let ignored_hashes: HashSet<&Hash> = HashSet::from([&blob3_hash]);
        let results =
            compute_budget_results(&resource_budget_blobs, &blob_count_by_hash, &ignored_hashes)?;
        let expected_result = vec![
            BudgetResult {
                name: "Component1".to_string(),
                budget_bytes: 123,
                creep_budget_bytes: 3245,
                used_bytes: 115,
                package_breakdown: HashMap::from([
                    (
                        package2_path.clone(),
                        PackageSizeInfo {
                            name: "".to_string(),
                            proportional_size: 70, /* 90/2 + 50/2 */
                            used_space_in_blobfs: 140,
                            blobs: vec![
                                PackageBlobSizeInfo {
                                    merkle: blob2_hash.clone(),
                                    used_space_in_blobfs: 50,
                                    share_count: 2,
                                    absolute_share_count: 2,
                                    path_in_package: "".to_string(),
                                },
                                PackageBlobSizeInfo {
                                    merkle: blob1_hash.clone(),
                                    path_in_package: "".to_string(),
                                    used_space_in_blobfs: 90,
                                    share_count: 2,
                                    absolute_share_count: 2,
                                },
                            ],
                        },
                    ),
                    (
                        package1_path.clone(),
                        PackageSizeInfo {
                            name: "".to_string(),
                            proportional_size: 45, /* 90/2 */
                            used_space_in_blobfs: 90,
                            blobs: vec![PackageBlobSizeInfo {
                                merkle: blob1_hash.clone(),
                                path_in_package: "".to_string(),
                                used_space_in_blobfs: 90,
                                share_count: 2,
                                absolute_share_count: 2,
                            }],
                        },
                    ),
                ]),
            },
            BudgetResult {
                name: "Component2".to_string(),
                budget_bytes: 456,
                creep_budget_bytes: 111,
                used_bytes: 31, /* 25 + 6 */
                package_breakdown: HashMap::from([(
                    package3_path.clone(),
                    PackageSizeInfo {
                        name: "".to_string(),
                        proportional_size: 25, /* 50/2 */
                        used_space_in_blobfs: 50,
                        blobs: vec![PackageBlobSizeInfo {
                            merkle: blob2_hash.clone(),
                            path_in_package: "".to_string(),
                            used_space_in_blobfs: 50,
                            share_count: 2,
                            absolute_share_count: 2,
                        }],
                    },
                )]),
            },
        ];
        assert_eq!(results, expected_result);
        Ok(())
    }
}
