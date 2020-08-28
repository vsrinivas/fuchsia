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
    anyhow::{anyhow, bail, Context, Result},
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

pub use cache::{env_file, init_config};
pub use config_macros::{get, print, remove, set};

#[cfg(not(test))]
pub use cache::get_config_base_path;

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum ConfigLevel {
    Default,
    Build,
    Global,
    User,
    Runtime,
}

pub async fn get_config(name: &str) -> Result<Option<Value>> {
    get_config_with_build_dir(name, &None, identity).await
}

pub async fn get_config_sub(name: &str) -> Result<Option<Value>> {
    get_config_with_build_dir(name, &None, flatten_env_var).await
}

pub async fn get_config_with_build_dir(
    name: &str,
    build_dir: &Option<String>,
    mapper: ConfigMapper,
) -> Result<Option<Value>> {
    let config = load_config(build_dir).await?;
    let read_guard = config.read().await;
    Ok((*read_guard).get(name, mapper))
}

pub async fn get_config_str(name: &str, default: &str) -> String {
    get_config_with_build_dir(name, &None, flatten_env_var)
        .await
        .unwrap_or(Some(Value::String(default.to_string())))
        .map_or(default.to_string(), |v| v.as_str().unwrap_or(default).to_string())
}

pub async fn try_get_config_str(name: &str) -> Result<Option<String>> {
    Ok(get_config_with_build_dir(name, &None, flatten_env_var)
        .await?
        .and_then(|v| v.as_str().map(|s| s.to_string())))
}

pub async fn try_get_config_file(name: &str) -> Result<Option<PathBuf>> {
    Ok(get_config_with_build_dir(name, &None, file_flatten_env_var)
        .await?
        .and_then(|v| v.as_str().map(|s| PathBuf::from(s.to_string()))))
}

pub async fn try_get_config_file_str(name: &str) -> Result<Option<String>> {
    Ok(get_config_with_build_dir(name, &None, file_flatten_env_var)
        .await?
        .and_then(|v| v.as_str().map(|s| s.to_string())))
}

pub async fn get_config_bool(name: &str, default: bool) -> bool {
    get_config_with_build_dir(name, &None, flatten_env_var)
        .await
        .unwrap_or(Some(Value::Bool(default)))
        .map_or(default, |v| {
            v.as_bool().unwrap_or(match v {
                Value::String(s) => s.parse().unwrap_or(default),
                _ => default,
            })
        })
}

pub async fn get_config_number(name: &str, default: u64) -> u64 {
    get_config_with_build_dir(name, &None, flatten_env_var)
        .await
        .unwrap_or(Some(Value::Number(serde_json::Number::from(default))))
        .map_or(default, |v| {
            v.as_u64().unwrap_or(match v {
                Value::String(s) => s.parse().unwrap_or(default),
                _ => default,
            })
        })
}

pub async fn try_get_config_number(name: &str) -> Result<Option<u64>> {
    Ok(get_config_with_build_dir(name, &None, flatten_env_var).await?.and_then(|v| v.as_u64()))
}

pub async fn set_config(level: &ConfigLevel, name: &str, value: Value) -> Result<()> {
    set_config_with_build_dir(level, name, value, &None).await
}

pub async fn set_config_with_build_dir(
    level: &ConfigLevel,
    name: &str,
    value: Value,
    build_dir: &Option<String>,
) -> Result<()> {
    check_config_files(level, build_dir)?;
    let config = load_config(&build_dir).await?;
    let mut write_guard = config.write().await;
    (*write_guard).set(&level, &name, value)?;
    save_config(&mut *write_guard, build_dir)
}

fn check_config_files(level: &ConfigLevel, build_dir: &Option<String>) -> Result<()> {
    let e = env_file().ok_or(anyhow!("Could not find environment file"))?;
    let mut environment = Environment::load(&e)?;
    match level {
        ConfigLevel::User => {
            if let None = environment.user {
                let default_path = get_default_user_file_path();
                // This will override the config file if it exists.  This would happen anyway
                // because of the cache.
                let mut file = File::create(&default_path).context("opening write buffer")?;
                file.write_all(b"{}").context("writing default user configuration file")?;
                file.sync_all().context("syncing default user configuration file to filesystem")?;
                environment.user = Some(
                    default_path
                        .to_str()
                        .map(|s| s.to_string())
                        .context("home path is not proper unicode")?,
                );
                environment.save(&e)?;
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
    Ok(())
}

pub async fn remove_config(level: &ConfigLevel, name: &str) -> Result<()> {
    remove_config_with_build_dir(level, name, &None).await
}

pub async fn remove_config_with_build_dir(
    level: &ConfigLevel,
    name: &str,
    build_dir: &Option<String>,
) -> Result<()> {
    let config = load_config(&build_dir).await?;
    let mut write_guard = config.write().await;
    (*write_guard).remove(&level, &name)?;
    save_config(&mut *write_guard, build_dir)
}

pub mod logging {
    use {
        anyhow::{Context as _, Result},
        simplelog::{
            CombinedLogger, Config, ConfigBuilder, LevelFilter, SimpleLogger, TermLogger,
            TerminalMode, WriteLogger,
        },
        std::fs::{create_dir_all, OpenOptions},
        std::path::PathBuf,
    };

    const LOG_DIR: &str = "log.dir";
    const LOG_ENABLED: &str = "log.enabled";

    fn config() -> Config {
        // Sets the target level to "Error" so that all logs show their module
        // target in the logs.
        ConfigBuilder::new().set_target_level(LevelFilter::Error).build()
    }

    async fn log_dir() -> PathBuf {
        let default_log_dir = {
            let mut path = ffx_core::get_base_path();
            path.push("logs");
            path.to_string_lossy().into_owned()
        };
        PathBuf::from(super::get_config_str(LOG_DIR, &default_log_dir).await)
    }

    pub async fn log_file(name: &str) -> Result<std::fs::File> {
        let mut log_path = log_dir().await;
        create_dir_all(&log_path).expect("cannot create log directory");
        log_path.push(format!("{}.log", name));
        OpenOptions::new()
            .write(true)
            .append(true)
            .create(true)
            .open(log_path)
            .context("opening log file")
    }

    pub async fn is_enabled() -> bool {
        super::get_config_bool(LOG_ENABLED, false).await
    }

    pub async fn init(stdio: bool) -> Result<()> {
        let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> =
            vec![TermLogger::new(LevelFilter::Error, Config::default(), TerminalMode::Mixed)
                .context("initializing terminal error logger")?];

        if is_enabled().await {
            // The daemon logs to stdio, and is redirected to file by spawn_daemon,
            // which enables panics and backtraces to also be included.
            if stdio {
                loggers.push(SimpleLogger::new(LevelFilter::Debug, config()));
            } else {
                let file = log_file("ffx").await?;
                loggers.push(WriteLogger::new(LevelFilter::Debug, config(), file));
            }
        }

        CombinedLogger::init(loggers).context("initializing logger")
    }
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

pub fn save_config(config: &mut Config, build_dir: &Option<String>) -> Result<()> {
    let e = env_file().ok_or(anyhow!("Could not find environment file"))?;
    let env = Environment::load(&e)?;
    build_dir.as_ref().map_or(config.save(&env.global, &None, &env.user), |b| {
        config.save(&env.global, &env.build.as_ref().and_then(|c| c.get(b)), &env.user)
    })
}

pub async fn print_config<W: Write>(mut writer: W, build_dir: &Option<String>) -> Result<()> {
    let config = load_config(build_dir).await?;
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
        levels.iter().for_each(|level| {
            let result = check_config_files(&level, &build_dir);
            assert!(result.is_err());
        });
    }
}
