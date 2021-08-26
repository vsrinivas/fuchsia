// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use ffx_assembly_args::ConfigDataArgs;
use ffx_assembly_args::ConfigDataChange;
use fuchsia_archive;
use fuchsia_pkg::CreationManifest;
use std::collections::BTreeMap;
use std::fs::{create_dir_all, metadata, remove_dir_all, File};
use std::io::prelude::*;
use std::io::Cursor;
use std::path::PathBuf;
use std::str;

/// List of packages for which config data can be added if they already exist.
/// Config data for packages not already in config-data will always be allowed.
const ALLOWLIST: &'static [&'static str] = &["session_launcher", "session_manager"];

/// Creates a new config data package using supplied package and changes.
pub fn create_config_data(args: ConfigDataArgs) -> Result<()> {
    let mut temp_dir = PathBuf::new();
    if args.out_path.exists() && metadata(&args.out_path)?.is_dir() {
        temp_dir.push(&args.out_path);
    } else {
        temp_dir.push(&args.out_path.parent().ok_or(anyhow!("No parent for directory"))?);
    }
    temp_dir.push("temp");
    create_dir_all(&temp_dir)?;

    let result = create_config_data_with_temp_folder(args, &temp_dir);

    remove_dir_all(&temp_dir)?;

    result
}

fn create_config_data_with_temp_folder(args: ConfigDataArgs, temp_dir: &PathBuf) -> Result<()> {
    let mut pkg_file = File::open(&args.meta_far)?;
    let mut pkg_buffer = Vec::new();
    pkg_file.read_to_end(&mut pkg_buffer)?;

    let mut far_contents =
        extract_config_far(&mut pkg_buffer, &temp_dir).context("error extracting config data")?;

    check_allowlist(&args.changes, &far_contents)?;

    add_changes(&args.changes, &mut far_contents)?;

    let manifest = CreationManifest::from_external_and_far_contents(BTreeMap::new(), far_contents)?;

    let mut meta_far_path = args.out_path;
    if meta_far_path.exists() && metadata(&meta_far_path)?.is_dir() {
        meta_far_path = meta_far_path.join("meta.far");
    } else {
        create_dir_all(meta_far_path.parent().ok_or(anyhow!("No parent for directory"))?)?;
    }

    fuchsia_pkg::build(&manifest, meta_far_path, "config-data")?;

    Ok(())
}

/// Check if we are trying to add config data for an existing package that isn't in the allowlist.
/// The |existing_files| contains a map of paths relative to the config_data package to their paths
/// on disk.
fn check_allowlist(
    changes: &Vec<ConfigDataChange>,
    existing_files: &BTreeMap<String, String>,
) -> Result<()> {
    for change in changes.iter() {
        if !ALLOWLIST.contains(&change.package.as_str()) {
            let mut package_exists = false;
            for file in existing_files.keys() {
                // The package has existing config data if there are any files that have a path of
                // meta/data/[package_name]/...
                let path = format!("meta/data/{}", &change.package.as_str());
                if file.starts_with(&path) {
                    package_exists = true;
                }
            }
            if package_exists {
                anyhow::bail!(
                    "Allowlist missing package: {}, Allowed packages: {:?}",
                    change.package,
                    ALLOWLIST
                );
            }
        }
    }
    Ok(())
}

/// Extracts the data from the config data's far into the temp directory, and returns a map of
/// filename to file path on disk.
fn extract_config_far(
    meta_far: &mut Vec<u8>,
    tmp_dir: &PathBuf,
) -> Result<BTreeMap<String, String>> {
    let mut meta_files = BTreeMap::new();

    let mut cursor = Cursor::new(meta_far);
    let mut far = fuchsia_archive::Reader::new(&mut cursor)?;

    let pkg_files: Vec<String> = far.list().map(|e| e.path().to_string()).collect();
    // Extract all the far meta files.
    for file_name in pkg_files.iter() {
        // Meta contents is generated, so we don't copy it out
        if file_name != "meta/contents" {
            let data = far.read_file(file_name)?;
            let file_path = tmp_dir.join(file_name);
            if let Some(parent_dir) = file_path.as_path().parent() {
                create_dir_all(parent_dir)?;
            }
            let mut package_file = File::create(&file_path)?;
            package_file.write_all(&data)?;

            if let Some(path_str) = file_path.to_str() {
                meta_files.insert(file_name.clone(), path_str.to_string());
            } else {
                anyhow::bail!(
                    "File must be a utf-8 string right now but shouldn't have to be: {:?}",
                    file_path
                );
            }
        }
    }

    Ok(meta_files)
}

/// Modifies the |meta_files| map with the changes in |config_changes|.
/// All changes are presumed to be valid, and will overwrite any existing files.
fn add_changes(
    config_changes: &Vec<ConfigDataChange>,
    meta_files: &mut BTreeMap<String, String>,
) -> Result<()> {
    for change in config_changes.iter() {
        if change.destination.is_absolute() {
            anyhow::bail!(
                "Destination must be a relative path to the package: {:?}",
                change.destination
            );
        }

        // The config data package stores data per package under meta/data/[package_name]/.
        let mut destination = PathBuf::new();
        destination.push("meta");
        destination.push("data");
        destination.push(change.package.as_str());
        destination.push(&change.destination);

        // The underlying CreationManifest takes in strings instead of paths.
        if let Some(dest_str) = destination.to_str() {
            if let Some(file_str) = change.file.to_str() {
                meta_files.insert(dest_str.to_string(), file_str.to_string());
            } else {
                anyhow::bail!(
                    "Source must be a utf-8 string right now but shouldn't have to be: {:?}",
                    change.file
                );
            }
        } else {
            anyhow::bail!(
                "Destination must be a utf-8 string right now but shouldn't have to be: {:?}",
                destination
            );
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn create_test_far(temp_dir: &TempDir) -> PathBuf {
        let data_dir = temp_dir.path().to_path_buf().join("test");
        create_dir_all(&data_dir).unwrap();

        let startup_config_path = data_dir.join("startup.config");
        let mut startup_config = File::create(&startup_config_path).unwrap();
        writeln!(startup_config, "Startup config contents").unwrap();

        let service_flags_path = data_dir.join("service_flags.json");
        let mut service_flags = File::create(&service_flags_path).unwrap();
        writeln!(service_flags, "Service flag contents").unwrap();

        let meta_package_path = data_dir.join("package");
        let mut meta_package = File::create(&meta_package_path).unwrap();
        writeln!(meta_package, "{{\"name\":\"config-data\",\"version\":\"0\"}}").unwrap();

        let mut far_contents = BTreeMap::new();
        far_contents.insert(
            "meta/data/session_manager/startup.config".to_string(),
            startup_config_path.to_str().unwrap().to_string(),
        );
        far_contents.insert(
            "meta/data/setui_service/service_flags.json".to_string(),
            service_flags_path.to_str().unwrap().to_string(),
        );
        far_contents
            .insert("meta/package".to_string(), meta_package_path.to_str().unwrap().to_string());

        let manifest =
            CreationManifest::from_external_and_far_contents(BTreeMap::new(), far_contents)
                .unwrap();

        let meta_far_path = data_dir.join("meta.far");

        fuchsia_pkg::build(&manifest, meta_far_path.clone(), "config-data").unwrap();

        meta_far_path
    }

    #[test]
    fn runs_with_no_changes() {
        let temp_dir = TempDir::new().expect("failed to create tmp dir");
        let test_far_path = create_test_far(&temp_dir);

        let changes = Vec::new();
        let config_data_args = ConfigDataArgs {
            out_path: temp_dir.path().to_path_buf().join("meta.far"),
            meta_far: test_far_path,
            changes: changes,
        };
        create_config_data(config_data_args).unwrap();
    }

    #[test]
    fn allowlist_only() {
        let temp_dir = TempDir::new().expect("failed to create tmp dir");

        let test_far_path = create_test_far(&temp_dir);
        let new_file_path = temp_dir.path().join("file.txt");

        let mut new_file = File::create(&new_file_path).unwrap();
        writeln!(new_file, "Test file.").unwrap();

        // is in config data but not in allowlist.
        let changes = vec![ConfigDataChange {
            package: "setui_service".into(),
            file: new_file_path.clone(),
            destination: "file.name".into(),
        }];
        let config_data_args = ConfigDataArgs {
            out_path: temp_dir.path().to_path_buf().join("meta.far"),
            meta_far: test_far_path.clone(),
            changes: changes,
        };
        assert!(create_config_data(config_data_args).is_err());

        // is not in allowlist or config_data.
        let changes = vec![ConfigDataChange {
            package: "not_in_allowlist".into(),
            file: new_file_path.clone(),
            destination: "file.name".into(),
        }];
        let config_data_args = ConfigDataArgs {
            out_path: temp_dir.path().to_path_buf().join("meta.far"),
            meta_far: test_far_path.clone(),
            changes: changes,
        };
        create_config_data(config_data_args).unwrap();

        // is in allowlist and config_data.
        let changes = vec![ConfigDataChange {
            package: "session_launcher".into(),
            file: new_file_path.clone(),
            destination: "file.name".into(),
        }];
        let config_data_args = ConfigDataArgs {
            out_path: temp_dir.path().to_path_buf().join("meta.far"),
            meta_far: test_far_path.clone(),
            changes: changes,
        };
        create_config_data(config_data_args).unwrap();
    }

    #[test]
    fn adds_file_correctly() {
        let string1 = "test file 1";
        let string2 = "test file 2";

        let temp_dir = TempDir::new().expect("failed to create tmp dir");
        let test_far_path = create_test_far(&temp_dir);

        let new_file_path = temp_dir.path().join("file.txt");
        let mut new_file = File::create(&new_file_path).unwrap();

        let existing_file_path = temp_dir.path().join("startup.config");
        let mut existing_file = File::create(&existing_file_path).unwrap();

        write!(new_file, "{}", string1).unwrap();
        write!(existing_file, "{}", string2).unwrap();

        let changes = vec![
            ConfigDataChange {
                package: "session_manager".into(),
                file: new_file_path.clone(),
                destination: "test/new_file".into(),
            },
            ConfigDataChange {
                package: "session_manager".into(),
                file: existing_file_path.clone(),
                destination: "startup.config".into(),
            },
        ];

        let config_data_args = ConfigDataArgs {
            out_path: temp_dir.path().to_path_buf(),
            meta_far: test_far_path,
            changes: changes,
        };

        create_config_data(config_data_args).unwrap();

        let created_path = temp_dir.path().to_path_buf().join("meta.far");

        let mut pkg_file = File::open(&created_path).unwrap();
        let mut pkg_buffer = Vec::new();
        pkg_file.read_to_end(&mut pkg_buffer).unwrap();

        let mut cursor = Cursor::new(pkg_buffer);
        let mut far = fuchsia_archive::Reader::new(&mut cursor).unwrap();

        // adding a new config data file.
        let new_file_out = far.read_file("meta/data/session_manager/test/new_file").unwrap();
        assert_eq!(str::from_utf8(&new_file_out).unwrap(), string1);

        // changing an existing config data file.
        let existing_file_out = far.read_file("meta/data/session_manager/startup.config").unwrap();
        assert_eq!(str::from_utf8(&existing_file_out).unwrap(), string2);

        let unchanged_file = far.read_file("meta/data/setui_service/service_flags.json").unwrap();
        assert_eq!(str::from_utf8(&unchanged_file).unwrap(), "Service flag contents\n");
    }
}
