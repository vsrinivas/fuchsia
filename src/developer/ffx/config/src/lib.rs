// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::cache::load_config,
    crate::environment::Environment,
    crate::file_backed_config::FileBacked as Config,
    crate::mapping::{
        config::config, data::data, env_var::env_var, file_check::file_check, filter::filter,
        flatten::flatten, home::home, identity::identity,
    },
    anyhow::{anyhow, bail, Context, Result},
    serde_json::Value,
    std::{
        convert::{From, TryFrom, TryInto},
        default::Default,
        env::var,
        fs::{create_dir_all, File},
        io::Write,
        path::PathBuf,
    },
};

#[cfg(test)]
use tempfile::NamedTempFile;

pub mod constants;
pub mod environment;
pub mod logging;

mod cache;
mod file_backed_config;
mod mapping;
mod persistent_config;
mod priority_config;
mod runtime;

pub use cache::{env_file, init_config};

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

pub trait HandleArrays {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a>;
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
    T: TryFrom<ConfigValue> + HandleArrays,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    let env_var_mapper = env_var(&validate_type::<T>);
    let home_mapper = home(&env_var_mapper);
    let config_mapper = config(&home_mapper);
    let data_mapper = data(&config_mapper);
    let flatten_env_var_mapper = T::handle(&data_mapper);
    get_config(query.into(), &flatten_env_var_mapper).await.map_err(|e| e.into())?.try_into()
}

pub async fn file<'a, T, U>(query: U) -> std::result::Result<T, T::Error>
where
    T: TryFrom<ConfigValue> + HandleArrays,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    let file_check_mapper = file_check(&identity);
    let env_var_mapper = env_var(&file_check_mapper);
    let home_mapper = home(&env_var_mapper);
    let config_mapper = config(&home_mapper);
    let data_mapper = data(&config_mapper);
    let flatten_env_var_mapper = T::handle(&data_mapper);
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

pub(crate) fn get_config_base_path() -> Result<PathBuf> {
    let mut path = match var("XDG_CONFIG_HOME").map(PathBuf::from) {
        Ok(p) => p,
        Err(_) => {
            let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
            home.push(".local");
            home.push("share");
            home
        }
    };
    path.push("Fuchsia");
    path.push("ffx");
    path.push("config");
    create_dir_all(&path)?;
    Ok(path)
}

pub(crate) fn get_data_base_path() -> Result<PathBuf> {
    let mut path = match var("XDG_DATA_HOME").map(PathBuf::from) {
        Ok(p) => p,
        Err(_) => {
            let mut home = home::home_dir().ok_or(anyhow!("cannot find home directory"))?;
            home.push(".local");
            home.push("share");
            home
        }
    };
    path.push("Fuchsia");
    path.push("ffx");
    create_dir_all(&path)?;
    Ok(path)
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

impl HandleArrays for Value {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        flatten(next)
    }
}

impl TryFrom<ConfigValue> for Value {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value.0.ok_or(anyhow!("no value").into())
    }
}

impl HandleArrays for Option<Value> {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        flatten(next)
    }
}

impl HandleArrays for String {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        flatten(next)
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

impl HandleArrays for Option<String> {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        flatten(next)
    }
}

impl TryFrom<ConfigValue> for Option<String> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        Ok(value.0.and_then(|v| v.as_str().map(|s| s.to_string())))
    }
}

impl HandleArrays for u64 {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        flatten(next)
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

impl HandleArrays for bool {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        flatten(next)
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

impl HandleArrays for PathBuf {
    fn handle<'a, T: Fn(Value) -> Option<Value> + Sync>(
        next: &'a T,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        flatten(next)
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

impl<T: TryFrom<ConfigValue>> HandleArrays for Vec<T> {
    fn handle<'a, F: Fn(Value) -> Option<Value> + Sync>(
        next: &'a F,
    ) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
        filter(next)
    }
}

impl<T: TryFrom<ConfigValue>> TryFrom<ConfigValue> for Vec<T> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|val| match val.as_array() {
                Some(v) => {
                    let result: Vec<T> = v
                        .iter()
                        .filter_map(|i| ConfigValue(Some(i.clone())).try_into().ok())
                        .collect();
                    if result.len() > 0 {
                        Some(result)
                    } else {
                        None
                    }
                }
                None => ConfigValue(Some(val)).try_into().map(|x| vec![x]).ok(),
            })
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

pub async fn add<'a, U: Into<ConfigQuery<'a>>>(query: U, value: Value) -> Result<()> {
    let config_query: ConfigQuery<'a> = query.into();
    let level = if let Some(l) = config_query.level {
        l
    } else {
        bail!("level of configuration is required to add a value");
    };
    check_config_files(&level, &config_query.build_dir.map(String::from))?;
    let config = load_config(&config_query.build_dir.map(String::from)).await?;
    let mut write_guard = config.write().await;
    if let Some(mut current) = (*write_guard).get(&config_query, &identity) {
        if current.is_object() {
            bail!("cannot add a value to a subtree");
        } else {
            match current.as_array_mut() {
                Some(v) => {
                    v.push(value);
                    (*write_guard).set(&config_query, Value::Array(v.to_vec()))?;
                }
                None => {
                    (*write_guard).set(&config_query, Value::Array(vec![current, value]))?;
                }
            }
        }
    } else {
        (*write_guard).set(&config_query, value)?;
    }
    save_config(&mut *write_guard, &config_query.build_dir.map(String::from))
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
    let mut default_path = get_config_base_path().expect("cannot get configuration base path");
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

    #[test]
    fn test_converting_array() -> Result<()> {
        let c = |val: Value| -> ConfigValue { ConfigValue(Some(val)) };
        let conv_elem: Vec<String> = c(json!("test")).try_into()?;
        assert_eq!(1, conv_elem.len());
        let conv_string: Vec<String> = c(json!(["test", "test2"])).try_into()?;
        assert_eq!(2, conv_string.len());
        let conv_bool: Vec<bool> = c(json!([true, "false", false])).try_into()?;
        assert_eq!(3, conv_bool.len());
        let conv_bool_2: Vec<bool> = c(json!([36, "false", false])).try_into()?;
        assert_eq!(2, conv_bool_2.len());
        let conv_num: Vec<u64> = c(json!([3, "36", 1000])).try_into()?;
        assert_eq!(3, conv_num.len());
        let conv_num_2: Vec<u64> = c(json!([3, "false", 1000])).try_into()?;
        assert_eq!(2, conv_num_2.len());
        let bad_elem: std::result::Result<Vec<u64>, ConfigError> = c(json!("test")).try_into();
        assert!(bad_elem.is_err());
        let bad_elem_2: std::result::Result<Vec<u64>, ConfigError> = c(json!(["test"])).try_into();
        assert!(bad_elem_2.is_err());
        Ok(())
    }
}
