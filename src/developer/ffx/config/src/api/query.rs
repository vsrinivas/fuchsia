// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    cache::load_config, check_config_files, get_config, nested::RecursiveMap, save_config,
    validate_type, ConfigError, ConfigLevel, ConfigValue, ValueStrategy,
};
use anyhow::{bail, Result};
use serde_json::Value;
use std::{convert::From, default::Default};

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum SelectMode {
    First,
    All,
}

impl Default for SelectMode {
    fn default() -> Self {
        SelectMode::First
    }
}

#[derive(Debug, Default, Clone)]
pub struct ConfigQuery<'a> {
    pub name: Option<&'a str>,
    pub level: Option<ConfigLevel>,
    pub build_dir: Option<&'a str>,
    pub select: SelectMode,
}

impl<'a> ConfigQuery<'a> {
    pub fn new(
        name: Option<&'a str>,
        level: Option<ConfigLevel>,
        build_dir: Option<&'a str>,
        select: SelectMode,
    ) -> Self {
        Self { name, level, build_dir, select }
    }

    /// Adds the given name to the query and returns a new composed query.
    pub fn name(self, name: Option<&'a str>) -> Self {
        Self { name, ..self }
    }
    /// Adds the given level to the query and returns a new composed query.
    pub fn level(self, level: Option<ConfigLevel>) -> Self {
        Self { level, ..self }
    }
    /// Adds the given build to the query and returns a new composed query.
    pub fn build(self, build_dir: Option<&'a str>) -> Self {
        Self { build_dir, ..self }
    }
    /// Adds the given select mode to the query and returns a new composed query.
    pub fn select(self, select: SelectMode) -> Self {
        Self { select, ..self }
    }

    /// Get a value with as little processing as possible
    pub async fn get_raw<T>(&self) -> std::result::Result<T, T::Error>
    where
        T: TryFrom<ConfigValue> + ValueStrategy,
        <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    {
        T::validate_query(self)?;
        get_config(self).await.map_err(|e| e.into())?.recursive_map(&validate_type::<T>).try_into()
    }

    /// Get a value with the normal processing of substitution strings
    pub async fn get<T>(&self) -> std::result::Result<T, T::Error>
    where
        T: TryFrom<ConfigValue> + ValueStrategy,
        <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    {
        use crate::mapping::*;

        T::validate_query(self)?;

        get_config(self)
            .await
            .map_err(|e| e.into())?
            .recursive_map(&runtime)
            .recursive_map(&cache)
            .recursive_map(&data)
            .recursive_map(&config)
            .recursive_map(&home)
            .recursive_map(&env_var)
            .recursive_map(&T::handle_arrays)
            .recursive_map(&validate_type::<T>)
            .try_into()
    }

    /// Get a value with normal processing, but verifying that it's a file that exists.
    pub async fn get_file<T>(&self) -> std::result::Result<T, T::Error>
    where
        T: TryFrom<ConfigValue> + ValueStrategy,
        <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
    {
        use crate::mapping::*;

        T::validate_query(self)?;
        get_config(self)
            .await
            .map_err(|e| e.into())?
            .recursive_map(&runtime)
            .recursive_map(&cache)
            .recursive_map(&data)
            .recursive_map(&config)
            .recursive_map(&home)
            .recursive_map(&env_var)
            .recursive_map(&T::handle_arrays)
            .recursive_map(&file_check)
            .try_into()
    }

    /// Set the queried location to the given Value.
    pub async fn set(&self, value: Value) -> Result<()> {
        let level = if let Some(l) = self.level {
            l
        } else {
            bail!("level of configuration is required to set a value");
        };
        check_config_files(&level, &self.build_dir.map(String::from))?;
        let config = load_config(&self.build_dir.map(String::from)).await?;
        let mut write_guard = config.write().await;
        let config_changed = (*write_guard).set(&self, value)?;

        // FIXME(81502): There is a race between the ffx CLI and the daemon service
        // in updating the config. We can lose changes if both try to change the
        // config at the same time. We can reduce the rate of races by only writing
        // to the config if the value actually changed.
        if config_changed {
            save_config(&mut *write_guard, &self.build_dir.map(String::from))
        } else {
            Ok(())
        }
    }

    /// Remove the value at the queried location.
    pub async fn remove(&self) -> Result<()> {
        let config = load_config(&self.build_dir.map(String::from)).await?;
        let mut write_guard = config.write().await;
        (*write_guard).remove(&self)?;
        save_config(&mut *write_guard, &self.build_dir.map(String::from))
    }

    /// Add this value at the queried location as an array item, converting the location to an array
    /// if necessary.
    pub async fn add(&self, value: Value) -> Result<()> {
        let level = if let Some(l) = self.level {
            l
        } else {
            bail!("level of configuration is required to add a value");
        };
        check_config_files(&level, &self.build_dir.map(String::from))?;
        let config = load_config(&self.build_dir.map(String::from)).await?;
        let mut write_guard = config.write().await;
        let config_changed = if let Some(mut current) = (*write_guard).get(&self) {
            if current.is_object() {
                bail!("cannot add a value to a subtree");
            } else {
                match current.as_array_mut() {
                    Some(v) => {
                        v.push(value);
                        (*write_guard).set(&self, Value::Array(v.to_vec()))?
                    }
                    None => (*write_guard).set(&self, Value::Array(vec![current, value]))?,
                }
            }
        } else {
            (*write_guard).set(&self, value)?
        };

        // FIXME(81502): There is a race between the ffx CLI and the daemon service
        // in updating the config. We can lose changes if both try to change the
        // config at the same time. We can reduce the rate of races by only writing
        // to the config if the value actually changed.
        if config_changed {
            save_config(&mut *write_guard, &self.build_dir.map(String::from))
        } else {
            Ok(())
        }
    }
}

impl<'a> From<&'a str> for ConfigQuery<'a> {
    fn from(value: &'a str) -> Self {
        let name = Some(value);
        ConfigQuery { name, ..Default::default() }
    }
}

impl<'a> From<&'a String> for ConfigQuery<'a> {
    fn from(value: &'a String) -> Self {
        let name = Some(value.as_str());
        ConfigQuery { name, ..Default::default() }
    }
}

impl<'a> From<ConfigLevel> for ConfigQuery<'a> {
    fn from(value: ConfigLevel) -> Self {
        let level = Some(value);
        ConfigQuery { level, ..Default::default() }
    }
}
