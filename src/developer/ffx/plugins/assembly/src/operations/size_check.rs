// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::util::read_config;
use crate::util::write_json_file;
use anyhow::anyhow;
use anyhow::Result;
use ffx_assembly_args::SizeCheckArgs;
use fuchsia_hash::Hash;
use fuchsia_pkg::PackageManifest;
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::Path;
use std::path::PathBuf;
use url::Url;

/// Blob information. Entry of the "blobs.json" file.
#[derive(Debug, Deserialize)]
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
    pub creep_budget_bytes: u64,
    /// List of paths to `package_manifest.json` files for each package of the set of the group.    
    pub packages: Vec<PathBuf>,
    /// Blobs are de-duplicated by hash across the packages of this set.
    /// This is intended to approximate the process of merging packages.
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

/// List blobs that are charged to a given budget.
struct BudgetBlobs {
    /// Budget to which one the blobs applies.
    budget: BudgetResult,
    /// List of blobs hash and path that applies to that budget.
    blobs: Vec<(Hash, String)>,
}

/// Result data structure with the budget name, limits and used bytes.
#[derive(Serialize)]
struct BudgetResult {
    /// Human readable name of this budget.
    pub name: String,
    /// Number of bytes allotted to the packages this budget applies to.
    pub budget_bytes: u64,
    /// Allowed usage increase allowed for a given commit.
    pub creep_budget_bytes: u64,
    /// Number of bytes used by the packages this budget applies to.    
    pub used_bytes: u64,
}

/// Verifies that no budget is exceeded.
pub fn verify_budgets(args: SizeCheckArgs) -> Result<()> {
    // Read the budget configuration file.
    let config: BudgetConfig = read_config(&args.budgets)?;
    // List blobs hashes for each package manifest of each package budget.
    let package_budget_blobs = load_manifests_blobs_match_budgets(&config.package_set_budgets)?;
    let resource_budget_blobs =
        compute_resources_budget_blobs(&config.resource_budgets, &package_budget_blobs);

    // Read blob json file if any, and collect sizes on target.
    let blobs = load_blob_info(&args.blob_sizes)?;
    // Count how many times blobs are used.
    let blob_count_by_hash = count_blobs(&blobs, &package_budget_blobs)?;

    // Find blobs to be charged on the resource budget, and compute each budget usage.
    let mut results =
        compute_budget_results(&resource_budget_blobs, &blob_count_by_hash, &HashSet::new());

    // Compute the total size of the packages for each budget, excluding blobs charged on
    // resources budget.
    {
        let resource_hashes: HashSet<&Hash> =
            resource_budget_blobs.iter().flat_map(|b| &b.blobs).map(|b| &b.0).collect();
        results.append(&mut compute_budget_results(
            &package_budget_blobs,
            &blob_count_by_hash,
            &resource_hashes,
        ));
    }

    // Write the output result if requested by the command line.
    if let Some(out_path) = &args.gerrit_output {
        write_json_file(&out_path, &to_json_output(&results)?)?;
    }

    // Print a text report for each overrun budget.
    let over_budget = results.iter().filter(|e| e.used_bytes > e.budget_bytes).count();

    if over_budget > 0 {
        println!("FAILED: {} package set(s) over budget.", over_budget);
        for entry in results.iter() {
            if entry.used_bytes > entry.budget_bytes {
                println!(
                    " - \"{}\" is over budget (budget={}, usage={}, exceeding_by={})",
                    entry.name,
                    entry.budget_bytes,
                    entry.used_bytes,
                    entry.used_bytes - entry.budget_bytes
                );
            }
        }
        if let Some(out_path) = &args.gerrit_output {
            print!("Report written to {}", out_path.to_string_lossy());
        }
        Err(anyhow::Error::new(errors::ffx_error_with_code!(2, "FAILED: Size budget(s) exceeded")))
    } else {
        // Return Ok if every budget is respected.
        if let None = args.gerrit_output {
            print!(
                "SUCCESS: {} packages set(s) fit(s) within their respective budgets.",
                results.len()
            );
        }
        Ok(())
    }
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
            },
            blobs: Vec::new(),
        };

        for package in budget.packages.iter() {
            let manifest: PackageManifest = read_config(Path::new(package))?;
            for manifest_blob in manifest.into_blobs().drain(..) {
                budget_blob.blobs.push((manifest_blob.merkle, manifest_blob.path));
            }
        }

        if budget.merge {
            let mut map: HashMap<_, _> = budget_blob.blobs.drain(..).collect(); // dedupe.
            budget_blob.blobs = map.drain().collect();
        }

        budget_blobs.push(budget_blob);
    }
    Ok(budget_blobs)
}

/// Reads blob declaration file, and count how many times blobs are used.
fn load_blob_info(blob_size_paths: &Vec<PathBuf>) -> Result<Vec<BlobJsonEntry>> {
    let mut result = vec![];
    for blobs_path in blob_size_paths.iter() {
        let mut blobs: Vec<BlobJsonEntry> = read_config(&blobs_path)?;
        result.append(&mut blobs);
    }
    Ok(result)
}

// Reads blob declaration file, and count how many times blobs are used.
fn count_blobs(
    blob_sizes: &Vec<BlobJsonEntry>,
    blob_usages: &Vec<BudgetBlobs>,
) -> Result<HashMap<Hash, BlobSizeAndCount>> {
    let mut blob_count_by_hash = HashMap::new();
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

    for budget_usage in blob_usages.iter() {
        for (hash, _path) in budget_usage.blobs.iter() {
            match blob_count_by_hash.get_mut(hash) {
                Some(blob_entry_count) => {
                    blob_entry_count.share_count += 1;
                }
                None => {
                    println!(
                        "WARNING: Size budget {} blob {} not found",
                        budget_usage.budget.name, hash
                    )
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
            .filter(|(_hash, path)| budget.paths.contains(path))
            .map(|(hash, _path)| hash)
            .cloned()
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
            },
            blobs: package_budget_blobs
                .iter()
                .flat_map(|budget| &budget.blobs)
                .filter(|(hash, _path)| hashes.contains(hash))
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
) -> Vec<BudgetResult> {
    let mut result = vec![];
    for budget_usage in budget_usages.iter() {
        let used_bytes: u64 = budget_usage
            .blobs
            .iter()
            .filter(|(hash, _path)| !ignore_hashes.contains(hash))
            .map(|(hash, _path)| match blob_count_by_hash.get(hash) {
                Some(blob_entry_count) => blob_entry_count.size / blob_entry_count.share_count,
                None => 0,
            })
            .sum();
        result.push(BudgetResult {
            name: budget_usage.budget.name.clone(),
            used_bytes: used_bytes,
            ..budget_usage.budget
        });
    }
    result
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

    use crate::operations::size_check::verify_budgets;
    use crate::util::read_config;
    use crate::util::write_json_file;
    use errors::ResultExt;
    use ffx_assembly_args::SizeCheckArgs;
    use serde_json::json;
    use std::fs;
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
            write_json_file(&path, &value).unwrap()
        }

        fn assert_eq(&self, rel_path: &str, expected: serde_json::Value) {
            let path = self.root.path().join(rel_path);
            let actual: serde_json::Value = read_config(&path).unwrap();
            assert_eq!(actual, expected);
        }

        fn path(&self, rel_path: &str) -> std::path::PathBuf {
            self.root.path().join(rel_path)
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
    fn fails_because_of_missing_blobs_file() {
        let test_fs = TestFs::new();
        test_fs.write("size_budgets.json", json!({}));
        let err = verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: None,
        });
        assert_failed(err, "Unable to open file:");
    }

    #[test]
    fn fails_because_of_missing_budget_file() {
        let test_fs = TestFs::new();
        test_fs.write("blobs.json", json!([]));
        let err = verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: None,
        });
        assert_eq!(err.exit_code(), 1);
        assert_failed(err, "Unable to open file:");
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
        let res = verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: None,
        });
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
        verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: None,
        })
        .unwrap();
    }

    #[test]
    fn two_blobs_with_one_shared_fails_overbudget() {
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
        let res = verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: Some(test_fs.path("output.json")),
        });

        test_fs.assert_eq(
            "output.json",
            json!({
                "Software Deliver": 40i32,
                "Software Deliver.budget": 1i32,
                "Software Deliver.creepBudget": 2i32,
                "Software Deliver.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"
            }),
        );
        assert_eq!(res.exit_code(), 2);
        assert_failed(res, "FAILED: Size budget(s) exceeded");
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
        verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: Some(test_fs.path("output.json")),
        })
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
        verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: Some(test_fs.path("output.json")),
        })
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
        verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: Some(test_fs.path("output.json")),
        })
        .unwrap();

        test_fs.assert_eq(
            "output.json",
            json!({
                "Connectivity": 0i32,
                "Connectivity.creepBudget": 1i32,
                "Connectivity.budget": 7497932i32,
                "Connectivity.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3AConnectivity",
            }),
        );
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
        verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: Some(test_fs.path("output.json")),
        })
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
}
