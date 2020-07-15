// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{PersistentConfig, ReadConfig, WriteConfig},
    crate::cache::load_config,
    crate::config::Config,
    crate::environment::Environment,
    anyhow::{anyhow, Error},
    ffx_config_plugin_args::ConfigLevel,
    ffx_core::constants::ENV_FILE,
    ffx_lib_args::Ffx,
    serde_json::Value,
    std::{env, fs::File, io::Write, path::PathBuf},
};

mod api;
mod cache;
mod config;
mod env_var_config;
pub mod environment;
mod file_backed_config;
mod heuristic_config;
mod heuristic_fns;
mod persistent_config;
mod priority_config;
mod runtime_config;

pub use config_macros::{ffx_cmd, ffx_env, get, remove, set};

#[cfg(target_os = "linux")]
mod linux;

#[cfg(not(target_os = "linux"))]
mod not_linux;

pub async fn get_config(
    name: &str,
    ffx: Ffx,
    env: Result<String, Error>,
) -> Result<Option<Value>, Error> {
    get_config_with_build_dir(name, &None, ffx, env).await
}

pub async fn get_config_with_build_dir(
    name: &str,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String, Error>,
) -> Result<Option<Value>, Error> {
    let config = load_config(build_dir, ffx, &env).await?;
    let read_guard = config.read().await;
    Ok((*read_guard).get(name))
}

pub async fn get_config_str(
    name: &str,
    default: &str,
    ffx: Ffx,
    env: Result<String, Error>,
) -> String {
    get_config(name, ffx, env)
        .await
        .unwrap_or(Some(Value::String(default.to_string())))
        .map_or(default.to_string(), |v| v.as_str().unwrap_or(default).to_string())
}

pub async fn get_config_bool(
    name: &str,
    default: bool,
    ffx: Ffx,
    env: Result<String, Error>,
) -> bool {
    get_config(name, ffx, env).await.unwrap_or(Some(Value::Bool(default))).map_or(default, |v| {
        v.as_bool().unwrap_or(match v {
            Value::String(s) => s.parse().unwrap_or(default),
            _ => default,
        })
    })
}

// TODO: remove dead code allowance when used (if ever)
pub async fn set_config(
    level: &ConfigLevel,
    name: &str,
    value: Value,
    ffx: Ffx,
    env: Result<String, Error>,
) -> Result<(), Error> {
    set_config_with_build_dir(level, name, value, &None, ffx, env).await
}

pub async fn set_config_with_build_dir(
    level: &ConfigLevel,
    name: &str,
    value: Value,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String, Error>,
) -> Result<(), Error> {
    let config = load_config(&build_dir, ffx, &env).await?;
    let mut write_guard = config.write().await;
    (*write_guard).set(&level, &name, value)?;
    save_config(&mut *write_guard, build_dir, env)
}

// TODO: remove dead code allowance when used (if ever)
#[allow(dead_code)]
pub async fn remove_config(
    level: &ConfigLevel,
    name: &str,
    ffx: Ffx,
    env: Result<String, Error>,
) -> Result<(), Error> {
    remove_config_with_build_dir(level, name, &None, ffx, env).await
}

pub async fn remove_config_with_build_dir(
    level: &ConfigLevel,
    name: &str,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String, Error>,
) -> Result<(), Error> {
    let config = load_config(&build_dir, ffx, &env).await?;
    let mut write_guard = config.write().await;
    (*write_guard).remove(&level, &name)?;
    save_config(&mut *write_guard, build_dir, env)
}

// TODO(fxr/45489): replace with the dirs::config_dir when the crate is included in third_party
// https://docs.rs/dirs/1.0.5/dirs/fn.config_dir.html
fn find_env_dir() -> Result<String, Error> {
    match env::var("HOME").or_else(|_| env::var("HOMEPATH")) {
        Ok(dir) => Ok(dir),
        Err(e) => Err(anyhow!("Could not determing environment directory: {}", e)),
    }
}

fn init_env_file(path: &PathBuf) -> Result<(), Error> {
    let mut f = File::create(path)?;
    f.write_all(b"{}")?;
    f.sync_all()?;
    Ok(())
}

pub fn find_env_file() -> Result<String, Error> {
    let mut env_path = PathBuf::from(find_env_dir()?);
    env_path.push(ENV_FILE);

    if !env_path.is_file() {
        log::debug!("initializing environment {}", env_path.display());
        init_env_file(&env_path)?;
    }
    match env_path.to_str() {
        Some(f) => Ok(String::from(f)),
        None => Err(anyhow!("Could not find environment file")),
    }
}

pub fn save_config(
    config: &mut Config<'_>,
    build_dir: &Option<String>,
    env_file: Result<String, Error>,
) -> Result<(), Error> {
    let env = Environment::load(&env_file?)?;

    match build_dir {
        Some(b) => config.save(&env.global, &env.build.as_ref().and_then(|c| c.get(b)), &env.user),
        None => config.save(&env.global, &None, &env.user),
    }
}
