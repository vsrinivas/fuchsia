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
use std::collections::BTreeMap;
use std::collections::HashMap;
use std::path::Path;
use std::path::PathBuf;
use url::Url;

/// Blob information. Entry of the "blobs.json" file.
#[derive(Debug, Deserialize)]
pub struct BlobInfo {
    /// Path to the file.
    pub source_path: PathBuf,
    /// Hash of the head for the blob tree.
    pub merkle: Hash,
    /// Size of the raw content in bytes.
    #[serde(rename = "bytes")]
    pub size: u64,
    /// Size of the content, once compressed and aligned.
    #[serde(rename = "size")]
    pub size_on_target: u64,
}

/// Size budget for a set of packages.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct PackageSetBudget {
    /// Human readable name of the package set.
    pub name: String,
    /// Number of bytes allotted for the packages of this group.
    pub budget_bytes: u64,
    /// List of paths to `package_manifest.json` files for each package of the set of the group.
    pub packages: Vec<PathBuf>,
}

struct BlobCount {
    /// Size of the blob content, once compressed and aligned.
    size_on_target: u64,
    /// Number of time this blob is mentioned in a budget.
    share_count: u64,
}

struct BudgetBlobs {
    budget: PackageSetBudget,
    blobs: Vec<Hash>,
}

#[derive(Serialize)]
struct BudgetUsage {
    /// Human readable name of this budget.
    pub name: String,
    /// Number of bytes allotted to the packages this budget applies to.
    pub budget_bytes: u64,
    /// Number of bytes used by the packages this budget applies to.
    pub used_bytes: u64,
}

/// Verifies that each package set fits in the specified budget.
pub fn verify_budgets(args: SizeCheckArgs) -> Result<()> {
    // Reads the budget file, and the list of merkle root hashes for each package manifest.
    let hashes_by_budget = load_budgets_and_manifests(&args.budgets)?;

    // Reads blob declaration file, and count how many times blobs are used.
    let blob_count_by_hash =
        load_blobs_file_and_count_usages(args.blob_sizes.iter(), &hashes_by_budget)?;

    // Compute the total size of the packages for each budget.
    let budget_usages = compute_budget_usage(&hashes_by_budget, &blob_count_by_hash);

    // Writes the output result if requested by the command line.
    if let Some(out_path) = &args.gerrit_output {
        write_json_file(&out_path, &to_json_output(&budget_usages)?)?;
    }

    // Print a text report for each overrun budget.
    let over_budget = budget_usages.iter().filter(|e| e.used_bytes > e.budget_bytes).count();

    if over_budget > 0 {
        println!("FAILED: {} package set(s) over budget.", over_budget);
        for entry in budget_usages.iter() {
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
                budget_usages.len()
            );
        }
        Ok(())
    }
}

/// Reads the budget file, and then each mentioned package manifest.
/// Returns pairs of budget and the list of blobs hashes consuming this budget.
fn load_budgets_and_manifests(budgets_path: &PathBuf) -> Result<Vec<BudgetBlobs>> {
    let mut budget_blobs = Vec::new();
    let budgets: Vec<PackageSetBudget> = read_config(&budgets_path)?;
    for budget in budgets.iter() {
        let mut usage = BudgetBlobs { budget: budget.clone(), blobs: Vec::new() };
        for package in usage.budget.packages.iter() {
            let manifest: PackageManifest = read_config(Path::new(package))?;
            for manifest_blob in manifest.into_blobs().iter() {
                usage.blobs.push(manifest_blob.merkle);
            }
        }
        budget_blobs.push(usage);
    }
    Ok(budget_blobs)
}

/// Reads blob declaration file, and count how many times blobs are used.
fn load_blobs_file_and_count_usages<'a, I>(
    blob_size_paths: I,
    blob_usages: &Vec<BudgetBlobs>,
) -> Result<HashMap<Hash, BlobCount>>
where
    I: IntoIterator<Item = &'a PathBuf>,
{
    let mut blob_count_by_hash = HashMap::new();
    for blobs_path in blob_size_paths.into_iter() {
        let mut blobs: Vec<BlobInfo> = read_config(&blobs_path)?;
        for blob_entry in blobs.drain(..) {
            if let Some(previous) = blob_count_by_hash.insert(
                blob_entry.merkle,
                BlobCount { size_on_target: blob_entry.size_on_target, share_count: 0 },
            ) {
                if previous.size_on_target != blob_entry.size_on_target {
                    return Err(anyhow!(
                        "Two blobs with same hash {} but different sizes",
                        blob_entry.merkle
                    ));
                }
            }
        }
    }

    for budget_usage in blob_usages.iter() {
        for blob in budget_usage.blobs.iter() {
            match blob_count_by_hash.get_mut(&blob) {
                Some(blob_entry_count) => {
                    blob_entry_count.share_count += 1;
                }
                None => {
                    println!(
                        "WARNING: Size budget {} blob {} not found",
                        budget_usage.budget.name, blob
                    )
                }
            }
        }
    }

    Ok(blob_count_by_hash)
}

/// Computes the total size of each component taking into account blob sharing.
fn compute_budget_usage(
    budget_usages: &Vec<BudgetBlobs>,
    blob_count_by_hash: &HashMap<Hash, BlobCount>,
) -> Vec<BudgetUsage> {
    let mut result = Vec::new();
    for usage in budget_usages.iter() {
        let mut used_bytes: u64 = 0;
        for blob in usage.blobs.iter() {
            if let Some(blob_entry_count) = blob_count_by_hash.get(blob) {
                used_bytes += blob_entry_count.size_on_target / blob_entry_count.share_count;
            }
        }
        result.push(BudgetUsage {
            name: usage.budget.name.clone(),
            budget_bytes: usage.budget.budget_bytes,
            used_bytes: used_bytes,
        });
    }
    result
}

/// Builds a report with the gerrit size checker format from the computed component size and budget.
fn to_json_output(budget_usages: &Vec<BudgetUsage>) -> Result<BTreeMap<String, serde_json::Value>> {
    // Use an ordered map to ensure the output is readable and stable.
    let mut budget_output = BTreeMap::new();
    for entry in budget_usages.iter() {
        budget_output.insert(entry.name.clone(), json!(entry.used_bytes));
        budget_output.insert(format!("{}.budget", entry.name), json!(entry.budget_bytes));
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

    #[test]
    fn fails_because_of_missing_blobs_file() {
        let test_fs = TestFs::new();
        test_fs.write("size_budgets.json", json!([]));
        let err = verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: None,
        });
        match err {
            Ok(_) => panic!("Unexpected success."),
            Err(e) => assert!(e.to_string().starts_with("Unable to open file:")),
        }
    }

    #[test]
    fn fails_because_of_missing_budget() {
        let test_fs = TestFs::new();
        test_fs.write("blobs.json", json!([]));
        let err = verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: None,
        });
        assert_eq!(err.exit_code(), 1);
        match err {
            Ok(_) => panic!("Unexpected success."),
            Err(e) => assert!(e.to_string().starts_with("Unable to open file:")),
        }
    }

    #[test]
    fn duplicate_merkle_in_blobs_file_with_different_sizes_causes_failure() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!([{
                "name": "Software Delivery",
                "budget_bytes": 1i32,
                "packages": [],
            }]),
        );

        test_fs.write(
            "blobs.json",
            json!([{
                "source_path": "first_blob",
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "bytes": 4i32,
                "size": 8i32
            },{
                "source_path": "same_as_above",
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "bytes": 4i32,
                "size": 16i32
            }]),
        );
        let res = verify_budgets(SizeCheckArgs {
            budgets: test_fs.path("size_budgets.json"),
            blob_sizes: [test_fs.path("blobs.json")].to_vec(),
            gerrit_output: None,
        });
        match res {
            Ok(_) => panic!("Unexpected success."),
            Err(e)=> assert!(e.to_string().starts_with("Two blobs with same hash 0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51 but different sizes")),
        }
    }

    #[test]
    fn duplicate_merkle_in_blobs_with_same_size_are_fine() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!([{
                "name": "Software Deliver",
                "budget_bytes": 1i32,
                "packages": [],
            }]),
        );

        test_fs.write(
            "blobs.json",
            json!([{
                "source_path": "first_blob",
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "bytes": 4i32,
                "size": 16i32
            },{
                "source_path": "same_as_above",
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "bytes": 4i32,
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
            json!([{
                "name": "Software Deliver",
                "budget_bytes": 1i32,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                    test_fs.path("obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json"),
                ]
            }]),
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
                "source_path": "first_blob_in_blobs",
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "bytes": 4i32,
                "size": 8i32
            },{
                "source_path": "second_blob_used_by_two_packages_of_the_component",
                "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                "bytes": 4i32,
                "size": 32i32
            },{
                "source_path": "unrelated_blob",
                "merkle": "01ecd6256f89243e1f0f7d7022cc2e8eb059b06c941d334d9ffb108478749646",
                "bytes": 132i32,
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
                "Software Deliver.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"
            }),
        );
        assert_eq!(res.exit_code(), 2);
        match res {
            Ok(_) => panic!("Unexpected success."),
            Err(e) => assert!(e.to_string().starts_with("FAILED: Size budget(s) exceeded")),
        }
    }

    #[test]
    fn blob_size_are_summed_test() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!([{
                "name": "Software Deliver",
                "budget_bytes": 7497932i32,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                ]
            }]),
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
                "source_path": "first_blob_in_blobs",
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "bytes": 4i32,
                "size": 8i32
            },{
                "source_path": "second_blob_in_blobs",
                "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                "bytes": 4i32,
                "size": 32i32
            },{
                "source_path": "blobs_file_contains_other_blobs_not_related",
                "merkle": "01ecd6256f89243e1f0f7d7022cc2e8eb059b06c941d334d9ffb108478749646",
                "bytes": 32i32,
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
                "Software Deliver.owner":
                "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3ASoftware+Deliver"}),
        );
    }

    #[test]
    fn blob_shared_by_two_budgets_test() {
        let test_fs = TestFs::new();
        test_fs.write(
            "size_budgets.json",
            json!([{
                "name": "Software Deliver",
                "budget_bytes": 7497932i32,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                    test_fs.path("obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json"),
                ]
            },{
                "name": "Connectivity",
                "budget_bytes": 10884219,
                "packages": [
                    test_fs.path( "obj/src/connectivity/bluetooth/core/bt-gap/bt-gap/package_manifest.json"),]
            }]),
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
              "source_path": "first_blob_in_blobs",
              "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
              "bytes": 9i32,
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
                "Connectivity.budget": 10884219i32,
                "Connectivity.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3AConnectivity",
                "Software Deliver": 106i32,
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
            json!([{
                "name": "Connectivity",
                "budget_bytes": 7497932i32,
                "packages": [
                    test_fs.path("obj/src/sys/pkg/bin/pkg-cache/pkg-cache/package_manifest.json"),
                ]
            }]),
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
                "Connectivity.budget": 7497932i32,
                "Connectivity.owner": "http://go/fuchsia-size-stats/single_component/?f=component%3Ain%3AConnectivity",
            }),
        );
    }
}
