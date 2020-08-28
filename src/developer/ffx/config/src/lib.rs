// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::cache::load_config,
    crate::env_var::env_var,
    crate::environment::Environment,
    crate::file_backed_config::FileBacked as Config,
    crate::file_check::file_check,
    crate::flatten::flatten,
    crate::identity::identity,
    anyhow::{anyhow, bail, Context, Result},
    serde_json::Value,
    std::{
        convert::{From, TryFrom, TryInto},
        default::Default,
        fs::File,
        io::Write,
        path::PathBuf,
    },
};

#[cfg(test)]
use tempfile::NamedTempFile;

mod cache;
pub mod constants;
mod env_var;
pub mod environment;
mod file_backed_config;
mod file_check;
mod flatten;
mod identity;
mod persistent_config;
mod priority_config;
mod runtime;

pub use cache::{env_file, init_config};

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

pub type ConfigResult = Result<ConfigValue>;
pub struct ConfigValue(Option<Value>);
pub struct ConfigError(anyhow::Error);

pub struct ConfigQuery<'a> {
    name: Option<&'a str>,
    level: Option<ConfigLevel>,
    build_dir: Option<&'a str>,
}

impl<'a> Default for ConfigQuery<'a> {
    fn default() -> Self {
        Self { name: None, level: None, build_dir: None }
    }
}

pub async fn raw<'a, T, U>(query: U) -> std::result::Result<T, T::Error>
where
    T: TryFrom<ConfigValue>,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    get_config(query.into(), &validate_type::<T>).await.map_err(|e| e.into())?.try_into()
}

pub async fn get<'a, T, U>(query: U) -> std::result::Result<T, T::Error>
where
    T: TryFrom<ConfigValue>,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    let env_var_mapper = env_var(&validate_type::<T>);
    let flatten_env_var_mapper = flatten(&env_var_mapper);
    get_config(query.into(), &flatten_env_var_mapper).await.map_err(|e| e.into())?.try_into()
}

pub async fn file<'a, T, U>(query: U) -> std::result::Result<T, T::Error>
where
    T: TryFrom<ConfigValue>,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    let file_check_mapper = file_check(&identity);
    let env_var_mapper = env_var(&file_check_mapper);
    let flatten_env_var_mapper = flatten(&env_var_mapper);
    get_config(query.into(), &flatten_env_var_mapper).await.map_err(|e| e.into())?.try_into()
}

async fn get_config<'a, T: Fn(Value) -> Option<Value>>(
    query: ConfigQuery<'a>,
    mapper: &T,
) -> ConfigResult {
    let config = load_config(&query.build_dir.map(String::from)).await?;
    let read_guard = config.read().await;
    Ok((*read_guard).get(&query, mapper).into())
}

pub(crate) fn validate_type<T>(value: Value) -> Option<Value>
where
    T: TryFrom<ConfigValue>,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
{
    let result: std::result::Result<T, T::Error> = ConfigValue(Some(value.clone())).try_into();
    match result {
        Ok(_) => Some(value),
        Err(_) => None,
    }
}

impl<'a> From<&'a str> for ConfigQuery<'a> {
    fn from(value: &'a str) -> Self {
        ConfigQuery { name: Some(value), ..Default::default() }
    }
}

impl<'a> From<&'a String> for ConfigQuery<'a> {
    fn from(value: &'a String) -> Self {
        ConfigQuery { name: Some(&value), ..Default::default() }
    }
}

impl<'a> From<ConfigLevel> for ConfigQuery<'a> {
    fn from(value: ConfigLevel) -> Self {
        ConfigQuery { level: Some(value), ..Default::default() }
    }
}

impl<'a> From<(&'a str, ConfigLevel)> for ConfigQuery<'a> {
    fn from(value: (&'a str, ConfigLevel)) -> Self {
        ConfigQuery { name: Some(value.0), level: Some(value.1), ..Default::default() }
    }
}

impl<'a> From<(&'a str, &ConfigLevel)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &ConfigLevel)) -> Self {
        ConfigQuery { name: Some(value.0), level: Some(*value.1), ..Default::default() }
    }
}

impl<'a> From<(&'a str, &'a str)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &'a str)) -> Self {
        ConfigQuery { name: Some(value.0), build_dir: Some(value.1), ..Default::default() }
    }
}

impl<'a> From<(&'a str, &ConfigLevel, &'a str)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &ConfigLevel, &'a str)) -> Self {
        ConfigQuery { name: Some(value.0), level: Some(*value.1), build_dir: Some(value.2) }
    }
}

impl<'a> From<(&'a str, &'a ConfigLevel, &'a Option<String>)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &'a ConfigLevel, &'a Option<String>)) -> Self {
        ConfigQuery {
            name: Some(value.0),
            level: Some(*value.1),
            build_dir: value.2.as_ref().map(|s| s.as_str()),
        }
    }
}

impl<'a> From<(&'a String, &'a ConfigLevel, &'a Option<String>)> for ConfigQuery<'a> {
    fn from(value: (&'a String, &'a ConfigLevel, &'a Option<String>)) -> Self {
        ConfigQuery {
            name: Some(value.0.as_str()),
            level: Some(*value.1),
            build_dir: value.2.as_ref().map(|s| s.as_str()),
        }
    }
}

impl From<anyhow::Error> for ConfigError {
    fn from(value: anyhow::Error) -> Self {
        ConfigError(value)
    }
}

impl From<ConfigError> for anyhow::Error {
    fn from(value: ConfigError) -> Self {
        value.0
    }
}

impl From<ConfigError> for std::convert::Infallible {
    fn from(_value: ConfigError) -> Self {
        panic!("never going to happen")
    }
}

impl From<ConfigValue> for Option<Value> {
    fn from(value: ConfigValue) -> Self {
        value.0
    }
}

impl From<Option<Value>> for ConfigValue {
    fn from(value: Option<Value>) -> Self {
        ConfigValue(value)
    }
}

impl TryFrom<ConfigValue> for String {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| v.as_str().map(|s| s.to_string()))
            .ok_or(anyhow!("no configuration String value found").into())
    }
}

impl TryFrom<ConfigValue> for Option<String> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        Ok(value.0.and_then(|v| v.as_str().map(|s| s.to_string())))
    }
}

impl TryFrom<ConfigValue> for u64 {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_u64().or_else(|| {
                    if let Value::String(s) = v {
                        let parsed: Result<u64, _> = s.parse();
                        match parsed {
                            Ok(b) => Some(b),
                            Err(_) => None,
                        }
                    } else {
                        None
                    }
                })
            })
            .ok_or(anyhow!("no configuration Number value found").into())
    }
}

impl TryFrom<ConfigValue> for bool {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_bool().or_else(|| {
                    if let Value::String(s) = v {
                        let parsed: Result<bool, _> = s.parse();
                        match parsed {
                            Ok(b) => Some(b),
                            Err(_) => None,
                        }
                    } else {
                        None
                    }
                })
            })
            .ok_or(anyhow!("no configuration Boolean value found").into())
    }
}

impl TryFrom<ConfigValue> for PathBuf {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| v.as_str().map(|s| PathBuf::from(s.to_string())))
            .ok_or(anyhow!("no configuration value found").into())
    }
}

pub async fn set<'a, U: Into<ConfigQuery<'a>>>(query: U, value: Value) -> Result<()> {
    let config_query: ConfigQuery<'a> = query.into();
    let level = if let Some(l) = config_query.level {
        l
    } else {
        bail!("level of configuration is required to set a value");
    };
    check_config_files(&level, &config_query.build_dir.map(String::from))?;
    let config = load_config(&config_query.build_dir.map(String::from)).await?;
    let mut write_guard = config.write().await;
    (*write_guard).set(&config_query, value)?;
    save_config(&mut *write_guard, &config_query.build_dir.map(String::from))
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

pub async fn remove<'a, U: Into<ConfigQuery<'a>>>(query: U) -> Result<()> {
    let config_query: ConfigQuery<'a> = query.into();
    let config = load_config(&config_query.build_dir.map(String::from)).await?;
    let mut write_guard = config.write().await;
    (*write_guard).remove(&config_query)?;
    save_config(&mut *write_guard, &config_query.build_dir.map(String::from))
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
            path
        };
        super::get(LOG_DIR).await.unwrap_or(default_log_dir)
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
        super::get(LOG_ENABLED).await.unwrap_or(false)
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
    use serde_json::json;

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

    #[test]
    fn test_validating_types() {
        assert!(validate_type::<String>(json!("test")).is_some());
        assert!(validate_type::<String>(json!(1)).is_none());
        assert!(validate_type::<String>(json!(false)).is_none());
        assert!(validate_type::<String>(json!(true)).is_none());
        assert!(validate_type::<String>(json!({"test": "whatever"})).is_none());
        assert!(validate_type::<String>(json!(["test", "test2"])).is_none());
        assert!(validate_type::<bool>(json!(true)).is_some());
        assert!(validate_type::<bool>(json!(false)).is_some());
        assert!(validate_type::<bool>(json!("true")).is_some());
        assert!(validate_type::<bool>(json!("false")).is_some());
        assert!(validate_type::<bool>(json!(1)).is_none());
        assert!(validate_type::<bool>(json!("test")).is_none());
        assert!(validate_type::<bool>(json!({"test": "whatever"})).is_none());
        assert!(validate_type::<bool>(json!(["test", "test2"])).is_none());
        assert!(validate_type::<u64>(json!(2)).is_some());
        assert!(validate_type::<u64>(json!(100)).is_some());
        assert!(validate_type::<u64>(json!("100")).is_some());
        assert!(validate_type::<u64>(json!("0")).is_some());
        assert!(validate_type::<u64>(json!(true)).is_none());
        assert!(validate_type::<u64>(json!("test")).is_none());
        assert!(validate_type::<u64>(json!({"test": "whatever"})).is_none());
        assert!(validate_type::<u64>(json!(["test", "test2"])).is_none());
        assert!(validate_type::<PathBuf>(json!("/")).is_some());
        assert!(validate_type::<PathBuf>(json!("test")).is_some());
        assert!(validate_type::<PathBuf>(json!(true)).is_none());
        assert!(validate_type::<PathBuf>(json!({"test": "whatever"})).is_none());
        assert!(validate_type::<PathBuf>(json!(["test", "test2"])).is_none());
    }
}
