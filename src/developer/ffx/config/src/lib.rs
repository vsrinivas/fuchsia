// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{
        get_config,
        query::ConfigQuery,
        validate_type,
        value::{ConfigValue, ValueStrategy},
        ConfigError,
    },
    crate::cache::load_config,
    crate::environment::Environment,
    crate::file_backed_config::FileBacked as Config,
    crate::mapping::{
        config::config, data::data, env_var::env_var, file_check::file_check, home::home,
        identity::identity,
    },
    anyhow::{anyhow, bail, Context, Result},
    serde_json::Value,
    std::{
        convert::{From, TryFrom, TryInto},
        env::var,
        fs::{create_dir_all, File},
        io::Write,
        path::PathBuf,
    },
};

#[cfg(test)]
use tempfile::NamedTempFile;

pub mod api;
pub mod constants;
pub mod environment;
pub mod logging;
pub mod sdk;

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

impl argh::FromArgValue for ConfigLevel {
    fn from_arg_value(val: &str) -> Result<Self, String> {
        match val {
            "u" | "user" => Ok(ConfigLevel::User),
            "b" | "build" => Ok(ConfigLevel::Build),
            "g" | "global" => Ok(ConfigLevel::Global),
            _ => Err(String::from(
                "Unrecognized value. Possible values are \"user\",\"build\",\"global\".",
            )),
        }
    }
}

pub async fn raw<'a, T, U>(query: U) -> std::result::Result<T, T::Error>
where
    T: TryFrom<ConfigValue> + ValueStrategy,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    let converted_query = query.into();
    T::validate_query(&converted_query)?;
    get_config(converted_query, &validate_type::<T>).await.map_err(|e| e.into())?.try_into()
}

pub async fn get<'a, T, U>(query: U) -> std::result::Result<T, T::Error>
where
    T: TryFrom<ConfigValue> + ValueStrategy,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    let converted_query = query.into();
    T::validate_query(&converted_query)?;
    let env_var_mapper = env_var(&validate_type::<T>);
    let home_mapper = home(&env_var_mapper);
    let config_mapper = config(&home_mapper);
    let data_mapper = data(&config_mapper);
    let array_env_var_mapper = T::handle_arrays(&data_mapper);
    get_config(converted_query, &array_env_var_mapper).await.map_err(|e| e.into())?.try_into()
}

pub async fn file<'a, T, U>(query: U) -> std::result::Result<T, T::Error>
where
    T: TryFrom<ConfigValue> + ValueStrategy,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    U: Into<ConfigQuery<'a>>,
{
    let converted_query = query.into();
    T::validate_query(&converted_query)?;
    let file_check_mapper = file_check(&identity);
    let env_var_mapper = env_var(&file_check_mapper);
    let home_mapper = home(&env_var_mapper);
    let config_mapper = config(&home_mapper);
    let data_mapper = data(&config_mapper);
    let array_env_var_mapper = T::handle_arrays(&data_mapper);
    get_config(converted_query, &array_env_var_mapper).await.map_err(|e| e.into())?.try_into()
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

pub async fn get_sdk() -> Result<sdk::Sdk> {
    if let Ok(manifest) = get("sdk.root").await {
        if get("sdk.type").await.unwrap_or("".to_string()) == "in-tree" {
            sdk::Sdk::from_build_dir(manifest)
        } else {
            sdk::Sdk::from_sdk_dir(manifest)
        }
    } else {
        ffx_core::ffx_bail!(
            "SDK directory could not be found. Please set with \
                             `ffx config set sdk.root <PATH_TO_SDK_DIR>`"
        );
    }
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
