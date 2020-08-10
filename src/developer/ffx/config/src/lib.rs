// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{ConfigMapper, ReadConfig, WriteConfig},
    crate::cache::load_config,
    crate::environment::Environment,
    crate::file_backed_config::FileBacked as Config,
    crate::file_flatten_env_var::file_flatten_env_var,
    crate::flatten_env_var::flatten_env_var,
    crate::identity::identity,
    anyhow::{bail, Context, Result},
    ffx_config_plugin_args::ConfigLevel,
    ffx_lib_args::Ffx,
    serde_json::Value,
    std::{fs::File, io::Write, path::PathBuf},
};

#[cfg(test)]
use tempfile::NamedTempFile;

mod api;
mod cache;
pub mod constants;
mod env_var;
pub mod environment;
mod file_backed_config;
mod file_flatten_env_var;
mod flatten_env_var;
mod identity;
mod persistent_config;
mod priority_config;
mod runtime;

pub use config_macros::{ffx_cmd, ffx_env, get, print, remove, set};

pub async fn get_config(name: &str, ffx: Ffx, env: Result<String>) -> Result<Option<Value>> {
    get_config_with_build_dir(name, &None, ffx, env, identity).await
}

pub async fn get_config_sub(name: &str, ffx: Ffx, env: Result<String>) -> Result<Option<Value>> {
    get_config_with_build_dir(name, &None, ffx, env, flatten_env_var).await
}

pub async fn get_config_with_build_dir(
    name: &str,
    build_dir: &Option<String>,
    ffx: Ffx,
    env: Result<String>,
    mapper: ConfigMapper,
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
        .and_then(|v| v.as_str().map(|s| s.to_string())))
}

pub async fn try_get_config_file(
    name: &str,
    ffx: Ffx,
    env: Result<String>,
) -> Result<Option<PathBuf>> {
    Ok(get_config_with_build_dir(name, &None, ffx, env, file_flatten_env_var)
        .await?
        .and_then(|v| v.as_str().map(|s| PathBuf::from(s.to_string()))))
}

pub async fn try_get_config_file_str(
    name: &str,
    ffx: Ffx,
    env: Result<String>,
) -> Result<Option<String>> {
    Ok(get_config_with_build_dir(name, &None, ffx, env, file_flatten_env_var)
        .await?
        .and_then(|v| v.as_str().map(|s| s.to_string())))
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

pub async fn try_get_config_number(
    name: &str,
    ffx: Ffx,
    env: Result<String>,
) -> Result<Option<u64>> {
    Ok(get_config_with_build_dir(name, &None, ffx, env, flatten_env_var)
        .await?
        .and_then(|v| v.as_u64()))
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
    check_config_files(level, build_dir, &env)?;
    let config = load_config(&build_dir, ffx, &env).await?;
    let mut write_guard = config.write().await;
    (*write_guard).set(&level, &name, value)?;
    save_config(&mut *write_guard, build_dir, env)
}

fn check_config_files(
    level: &ConfigLevel,
    build_dir: &Option<String>,
    env: &Result<String>,
) -> Result<()> {
    if let Ok(e) = env.as_ref() {
        let mut environment = Environment::load(e)?;
        match level {
            ConfigLevel::User => {
                if let None = environment.user {
                    let default_path = get_default_user_file_path();
                    // This will override the config file if it exists.  This would happen anyway
                    // because of the cache.
                    let mut file = File::create(&default_path).context("opening write buffer")?;
                    file.write_all(b"{}").context("writing default user configuration file")?;
                    file.sync_all()
                        .context("syncing default user configuration file to filesystem")?;
                    environment.user = Some(
                        default_path
                            .to_str()
                            .map(|s| s.to_string())
                            .context("home path is not proper unicode")?,
                    );
                    environment.save(e)?;
                }
            }
            ConfigLevel::Global => {
                if let None = environment.global {
                    bail!(
                        "Global configuration not set. Use 'ffx config env set' command \
                        to setup the environment."
                    );
                }
            }
            ConfigLevel::Build => match build_dir {
                Some(b_dir) => match environment.build {
                    None => bail!(
                        "Build configuration not set for '{}'. Use 'ffx config env set' command \
                        to setup the environment.",
                        b_dir
                    ),
                    Some(b) => {
                        if let None = b.get(b_dir) {
                            bail!(
                                "Build configuration not set for '{}'. Use 'ffx config env \
                                set' command to setup the environment.",
                                b_dir
                            );
                        }
                    }
                },
                None => bail!("Cannot set a build configuration without a build directory."),
            },
            _ => bail!("This config level is not writable."),
        }
    }
    Ok(())
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

fn init_env_file(path: &PathBuf) -> Result<()> {
    let mut f = File::create(path)?;
    f.write_all(b"{}")?;
    f.sync_all()?;
    Ok(())
}

#[cfg(test)]
fn get_default_user_file_path() -> PathBuf {
    lazy_static::lazy_static! {
        static ref FILE: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
    }
    FILE.path().to_path_buf()
}

#[cfg(not(test))]
fn get_default_user_file_path() -> PathBuf {
    let mut default_path = get_config_base_path();
    default_path.push(constants::DEFAULT_USER_CONFIG);
    default_path
}

#[cfg(not(test))]
fn get_config_base_path() -> PathBuf {
    let mut path = ffx_core::get_base_path();
    path.push("config");
    std::fs::create_dir_all(&path).expect("unable to create ffx config directory");
    path
}

#[cfg(test)]
pub fn find_env_file() -> Result<String> {
    lazy_static::lazy_static! {
        static ref FILE: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
    }
    init_env_file(&FILE.path().to_path_buf())?;
    if let Some(path) = FILE.path().to_str() {
        Ok(path.to_string())
    } else {
        bail!("Unable to get temp file path");
    }
}

#[cfg(not(test))]
pub fn find_env_file() -> Result<String> {
    let ffx: Ffx = argh::from_env();

    let env_path = if let Some(f) = ffx.environment_file {
        PathBuf::from(f)
    } else {
        let mut path = get_config_base_path();
        path.push(constants::ENV_FILE);
        path
    };

    if !env_path.is_file() {
        log::debug!("initializing environment {}", env_path.display());
        init_env_file(&env_path)?;
    }
    env_path.to_str().map(String::from).context("getting environment file")
}

pub fn save_config(
    config: &mut Config,
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

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_check_config_files_fails() {
        let levels = vec![
            ConfigLevel::Runtime,
            ConfigLevel::Default,
            ConfigLevel::Global,
            ConfigLevel::Build,
        ];
        let build_dir = None;
        let env_file = find_env_file();
        levels.iter().for_each(|level| {
            let result = check_config_files(&level, &build_dir, &env_file);
            assert!(result.is_err());
        });
    }
}
