// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{PersistentConfig, ReadConfig, WriteConfig},
    crate::cache::load_config,
    crate::config::Config,
    crate::constants::ENV_FILE,
    crate::environment::Environment,
    crate::flatten_env_var::flatten_env_var,
    anyhow::{bail, Context, Result},
    ffx_config_plugin_args::ConfigLevel,
    ffx_lib_args::Ffx,
    serde_json::Value,
    std::{convert::identity, env, fs::File, io::Write, path::PathBuf},
};

mod api;
mod cache;
mod config;
pub mod constants;
mod env_var;
pub mod environment;
mod file_backed_config;
mod flatten_env_var;
mod heuristic_config;
mod heuristic_fns;
mod persistent_config;
mod priority_config;
mod runtime_config;

pub use config_macros::{ffx_cmd, ffx_env, get, print, remove, set};

#[cfg(target_os = "linux")]
mod linux;

#[cfg(not(target_os = "linux"))]
mod not_linux;

pub async fn get_config(name: &str, ffx: Ffx, env: Result<String>) -> Result<Option<Value>> {
    get_config_with_build_dir(name, &None, ffx, env, identity).await
}

pub async fn get_config_with_build_dir(
    name: &str,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String>,
    mapper: fn(Option<Value>) -> Option<Value>,
) -> Result<Option<Value>> {
    let config = load_config(build_dir, ffx, &env).await?;
    let read_guard = config.read().await;
    Ok((*read_guard).get(name, mapper))
}

pub async fn get_config_str(name: &str, default: &str, ffx: Ffx, env: Result<String>) -> String {
    get_config_with_build_dir(name, &None, ffx, env, flatten_env_var)
        .await
        .unwrap_or(Some(Value::String(default.to_string())))
        .map_or(default.to_string(), |v| v.as_str().unwrap_or(default).to_string())
}

pub async fn try_get_config_str(
    name: &str,
    ffx: Ffx,
    env: Result<String>,
) -> Result<Option<String>> {
    Ok(get_config_with_build_dir(name, &None, ffx, env, flatten_env_var)
        .await?
        .map(|v| v.as_str().map(|s| s.to_string()))
        .flatten())
}

pub async fn get_config_bool(name: &str, default: bool, ffx: Ffx, env: Result<String>) -> bool {
    get_config_with_build_dir(name, &None, ffx, env, flatten_env_var)
        .await
        .unwrap_or(Some(Value::Bool(default)))
        .map_or(default, |v| {
            v.as_bool().unwrap_or(match v {
                Value::String(s) => s.parse().unwrap_or(default),
                _ => default,
            })
        })
}

pub async fn set_config(
    level: &ConfigLevel,
    name: &str,
    value: Value,
    ffx: Ffx,
    env: Result<String>,
) -> Result<()> {
    set_config_with_build_dir(level, name, value, &None, ffx, env).await
}

pub async fn set_config_with_build_dir(
    level: &ConfigLevel,
    name: &str,
    value: Value,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String>,
) -> Result<()> {
    let config = load_config(&build_dir, ffx, &env).await?;
    let mut write_guard = config.write().await;
    (*write_guard).set(&level, &name, value)?;
    save_config(&mut *write_guard, build_dir, env)
}

pub async fn remove_config(
    level: &ConfigLevel,
    name: &str,
    ffx: Ffx,
    env: Result<String>,
) -> Result<()> {
    remove_config_with_build_dir(level, name, &None, ffx, env).await
}

pub async fn remove_config_with_build_dir(
    level: &ConfigLevel,
    name: &str,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String>,
) -> Result<()> {
    let config = load_config(&build_dir, ffx, &env).await?;
    let mut write_guard = config.write().await;
    (*write_guard).remove(&level, &name)?;
    save_config(&mut *write_guard, build_dir, env)
}

// TODO(fxr/45489): replace with the dirs::config_dir when the crate is included in third_party
// https://docs.rs/dirs/1.0.5/dirs/fn.config_dir.html
fn find_env_dir() -> Result<String> {
    env::var("HOME")
        .or_else(|_| env::var("HOMEPATH"))
        .or_else(|e| bail!("Could not determing environment directory: {}", e))
}

fn init_env_file(path: &PathBuf) -> Result<()> {
    let mut f = File::create(path)?;
    f.write_all(b"{}")?;
    f.sync_all()?;
    Ok(())
}

// This method should not be called from unit tests since it tries to parse CLI params.
pub fn find_env_file() -> Result<String> {
    let ffx: Ffx = argh::from_env();

    let env_path = if let Some(f) = ffx.environment_file {
        PathBuf::from(f)
    } else {
        let mut path = PathBuf::from(find_env_dir()?);
        path.push(ENV_FILE);
        path
    };

    if !env_path.is_file() {
        log::debug!("initializing environment {}", env_path.display());
        init_env_file(&env_path)?;
    }
    env_path.to_str().map(String::from).context("getting environment file")
}

pub fn save_config(
    config: &mut Config<'_>,
    build_dir: &Option<String>,
    env_file: Result<String>,
) -> Result<()> {
    let env = Environment::load(&env_file?)?;

    build_dir.as_ref().map_or(config.save(&env.global, &None, &env.user), |b| {
        config.save(&env.global, &env.build.as_ref().and_then(|c| c.get(b)), &env.user)
    })
}

pub async fn print_config<W: Write>(
    mut writer: W,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String>,
) -> Result<()> {
    let config = load_config(build_dir, ffx, &env).await?;
    let read_guard = config.read().await;
    writeln!(writer, "{}", *read_guard).context("displaying config")
}
