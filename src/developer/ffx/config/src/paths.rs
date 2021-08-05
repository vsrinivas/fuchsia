// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    std::{fs::create_dir_all, path::PathBuf},
};

#[cfg(not(target_os = "macos"))]
use std::env::var;

#[cfg(test)]
use tempfile::NamedTempFile;

#[cfg(not(target_os = "macos"))]
fn get_runtime_base() -> Result<PathBuf> {
    var("XDG_RUNTIME_HOME").map(PathBuf::from).or_else(|_| {
        let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
        home.push(".local");
        home.push("share");
        Ok(home)
    })
}

#[cfg(target_os = "macos")]
fn get_runtime_base() -> Result<PathBuf> {
    let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
    home.push("Library");
    Ok(home)
}

pub(crate) fn get_runtime_base_path() -> Result<PathBuf> {
    let mut path = get_runtime_base()?;
    path.push("Fuchsia");
    path.push("ffx");
    path.push("runtime");
    create_dir_all(&path)?;
    Ok(path)
}

#[cfg(not(target_os = "macos"))]
fn get_cache_base() -> Result<PathBuf> {
    var("XDG_CACHE_HOME").map(PathBuf::from).or_else(|_| {
        let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
        home.push(".local");
        home.push("share");
        Ok(home)
    })
}

#[cfg(target_os = "macos")]
fn get_cache_base() -> Result<PathBuf> {
    let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
    home.push("Library");
    home.push("Caches");
    Ok(home)
}

pub(crate) fn get_cache_base_path() -> Result<PathBuf> {
    let mut path = get_cache_base()?;
    path.push("Fuchsia");
    path.push("ffx");
    path.push("cache");
    create_dir_all(&path)?;
    Ok(path)
}

#[cfg(not(target_os = "macos"))]
fn get_config_base() -> Result<PathBuf> {
    var("XDG_CONFIG_HOME").map(PathBuf::from).or_else(|_| {
        let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
        home.push(".local");
        home.push("share");
        Ok(home)
    })
}

#[cfg(target_os = "macos")]
fn get_config_base() -> Result<PathBuf> {
    let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
    home.push("Library");
    home.push("Preferences");
    Ok(home)
}

pub(crate) fn get_config_base_path() -> Result<PathBuf> {
    let mut path = get_config_base()?;
    path.push("Fuchsia");
    path.push("ffx");
    path.push("config");
    create_dir_all(&path)?;
    Ok(path)
}

pub fn default_env_path() -> Result<PathBuf> {
    // Environment file that keeps track of configuration files
    const ENV_FILE: &str = ".ffx_env";
    get_config_base_path().map(|mut path| {
        path.push(ENV_FILE);
        path
    })
}

#[cfg(not(target_os = "macos"))]
fn get_data_base() -> Result<PathBuf> {
    var("XDG_DATA_HOME").map(PathBuf::from).or_else(|_| {
        let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
        home.push(".local");
        home.push("share");
        Ok(home)
    })
}

#[cfg(target_os = "macos")]
fn get_data_base() -> Result<PathBuf> {
    let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
    home.push("Library");
    Ok(home)
}

pub(crate) fn get_data_base_path() -> Result<PathBuf> {
    let mut path = get_data_base()?;
    path.push("Fuchsia");
    path.push("ffx");
    create_dir_all(&path)?;
    Ok(path)
}

#[cfg(test)]
pub(crate) fn get_default_user_file_path() -> PathBuf {
    lazy_static::lazy_static! {
        static ref FILE: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
    }
    FILE.path().to_path_buf()
}

#[cfg(not(test))]
pub(crate) fn get_default_user_file_path() -> PathBuf {
    // Default user configuration file
    const DEFAULT_USER_CONFIG: &str = ".ffx_user_config.json";

    let mut default_path = get_config_base_path().expect("cannot get configuration base path");
    default_path.push(DEFAULT_USER_CONFIG);
    default_path
}
