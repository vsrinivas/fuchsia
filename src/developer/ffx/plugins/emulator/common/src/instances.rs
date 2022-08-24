// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module has common functions for helping commands resolve the emulator
//! instance that command is targeting, either by name or by default.
use crate::config::EMU_INSTANCE_ROOT_DIR;
use anyhow::{Context, Result};
use std::{fs::create_dir_all, path::PathBuf};

pub const SERIALIZE_FILE_NAME: &str = "engine.json";

/// Return a PathBuf with the path to the instance directory for this engine. If the "create" flag
/// is set, the directory and its ancestors will be created if it doesn't already exist.
pub async fn get_instance_dir(instance_name: &str, create: bool) -> Result<PathBuf> {
    let root_dir: String = ffx_config::get(EMU_INSTANCE_ROOT_DIR)
        .await
        .context("Error encountered accessing FFX config for the emulator instance root.")?;
    let path = PathBuf::from(root_dir).join(&instance_name);
    if !path.exists() {
        if create {
            tracing::debug!("Creating {:?} for {}", path, instance_name);
            create_dir_all(&path.as_path())?;
        } else {
            tracing::debug!(
                "Path {} doesn't exist. Check the spelling of the instance name.",
                instance_name
            );
        }
    }
    Ok(path)
}

/// Given an instance name, empty and remove the instance directory associated with that name.
/// Fails if the directory can't be removed; returns Ok(()) if the directory doesn't exist.
pub async fn clean_up_instance_dir(path: &PathBuf) -> Result<()> {
    if path.exists() {
        tracing::debug!("Removing {:?} for {:?}", path, path.as_path().file_name().unwrap());
        std::fs::remove_dir_all(&path.as_path()).context("Request to remove directory failed")
    } else {
        // It's already gone, so just return Ok(()).
        Ok(())
    }
}

/// Retrieve a list of all of the names of instances currently present on the local system.
pub async fn get_all_instances() -> Result<Vec<String>> {
    let mut result = Vec::new();
    let root_dir: String = ffx_config::get(EMU_INSTANCE_ROOT_DIR)
        .await
        .context("Error encountered accessing FFX config for the emulator instance root.")?;
    let buf = PathBuf::from(root_dir);
    let root = buf.as_path();
    if root.is_dir() {
        for entry in root.read_dir()? {
            if let Ok(entry) = entry {
                if !entry.path().is_dir() {
                    continue;
                }
                if entry.path().join(SERIALIZE_FILE_NAME).exists() {
                    if let Some(name_as_os_str) = entry.path().file_name() {
                        if let Some(name) = name_as_os_str.to_str() {
                            result.push(name.to_string());
                        }
                    }
                }
            }
        }
    }
    return Ok(result);
}

#[cfg(test)]
mod tests {
    use super::*;
    use ffx_config::{query, ConfigLevel};
    use serde_json::json;
    use std::fs::{remove_file, File};
    use tempfile::tempdir;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_instance_dir() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        query(EMU_INSTANCE_ROOT_DIR).level(Some(ConfigLevel::User)).set(json!(temp_dir)).await?;

        // Create a new directory.
        let path1 = get_instance_dir("create_me", true).await?;
        assert_eq!(path1, PathBuf::from(&temp_dir).join("create_me"));
        assert!(path1.exists());

        // Look for a dir that doesn't exist, but don't create it.
        let path2 = get_instance_dir("dont_create", false).await?;
        assert!(!path2.exists());

        // Look for a dir that already exists, but don't allow creation.
        let mut path3 = get_instance_dir("create_me", false).await?;
        assert_eq!(path3, PathBuf::from(&temp_dir).join("create_me"));
        assert!(path3.exists());

        // Get an existing directory, but set the create flag too. Make sure it didn't get replaced.
        path3 = path3.join("foo.txt");
        let _ = File::create(&path3)?;
        let path4 = get_instance_dir("create_me", true).await?;
        assert!(path4.exists());
        assert!(path3.exists());
        assert_eq!(path4, PathBuf::from(&temp_dir).join("create_me"));
        for entry in path4.as_path().read_dir()? {
            assert_eq!(entry?.path(), path3);
        }

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_all_instances() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();
        query(EMU_INSTANCE_ROOT_DIR).level(Some(ConfigLevel::User)).set(json!(temp_dir)).await?;

        // Create three mock instance directories, and make sure they're all included.
        let path1 = PathBuf::from(&temp_dir).join("path1");
        create_dir_all(path1.as_path())?;
        let file1_path = path1.join(SERIALIZE_FILE_NAME);
        let _file1 = File::create(&file1_path)?;

        let path2 = PathBuf::from(&temp_dir).join("path2");
        create_dir_all(path2.as_path())?;
        let file2_path = path2.join(SERIALIZE_FILE_NAME);
        let _file2 = File::create(&file2_path)?;

        let path3 = PathBuf::from(&temp_dir).join("path3");
        create_dir_all(path3.as_path())?;
        let file3_path = path3.join(SERIALIZE_FILE_NAME);
        let _file3 = File::create(&file3_path)?;

        let instances = get_all_instances().await?;
        assert!(instances.contains(&"path1".to_string()));
        assert!(instances.contains(&"path2".to_string()));
        assert!(instances.contains(&"path3".to_string()));

        // If the directory doesn't contain an engine.json file, it's not an instance.
        // Remove the file for path2, and make sure it's excluded from the results.
        assert!(remove_file(&file2_path).is_ok());

        let instances = get_all_instances().await?;
        assert!(instances.contains(&"path1".to_string()));
        assert!(!instances.contains(&"path2".to_string()));
        assert!(instances.contains(&"path3".to_string()));

        // Other files in the root shouldn't be included either. Create an empty file in the root
        // and make sure it's excluded too.
        let file_path = PathBuf::from(&temp_dir).join("empty_file");
        let _empty_file = File::create(&file_path)?;

        let instances = get_all_instances().await?;
        assert!(instances.contains(&"path1".to_string()));
        assert!(!instances.contains(&"path2".to_string()));
        assert!(instances.contains(&"path3".to_string()));
        assert!(!instances.contains(&"empty_file".to_string()));

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_clean_up_instance_dir() -> Result<()> {
        let temp_dir = tempdir()
            .expect("Couldn't get a temporary directory for testing.")
            .path()
            .to_str()
            .expect("Couldn't convert Path to str")
            .to_string();

        let path1 = PathBuf::from(&temp_dir).join("path1");
        create_dir_all(path1.as_path())?;
        assert!(path1.exists());

        let path2 = PathBuf::from(&temp_dir).join("path2");
        create_dir_all(path2.as_path())?;
        assert!(path2.exists());

        let file_path = path2.join("foo.txt");
        let _ = File::create(&file_path)?;
        assert!(file_path.exists());

        // Clean up an existing, empty directory
        assert!(clean_up_instance_dir(&path1).await.is_ok());
        assert!(!path1.exists());
        assert!(path2.exists());

        // Clean up an existing, populated directory
        assert!(clean_up_instance_dir(&path2).await.is_ok());
        assert!(!path2.exists());
        assert!(!file_path.exists());

        // Clean up an non-existing directory
        assert!(clean_up_instance_dir(&path1).await.is_ok());
        assert!(!path1.exists());
        Ok(())
    }
}
