// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{PersistentConfig, ReadConfig, WriteConfig},
    crate::environment::Environment,
    crate::file_backed_config::FileBacked,
    crate::runtime_config::Runtime,
    anyhow::{anyhow, Result},
    ffx_config_plugin_args::ConfigLevel,
    ffx_lib_args::Ffx,
    serde_json::Value,
    std::fmt,
};

pub struct Config {
    data: Option<FileBacked>,
    runtime: Runtime,
}

#[derive(Debug, PartialEq, Copy, Clone)]
enum ConfigOrder {
    Persistent,
    Runtime,
}

struct ReadConfigIterator<'a> {
    curr: Option<ConfigOrder>,
    config: &'a Config,
}

impl<'a> Iterator for ReadConfigIterator<'a> {
    type Item = Box<&'a (dyn ReadConfig + 'a)>;

    fn next(&mut self) -> Option<Self::Item> {
        // Check for configuration in this order:
        // 1. Runtime Configuration (set by the command line)
        // 2. Configuration Files
        match &self.curr {
            None => {
                self.curr = Some(ConfigOrder::Runtime);
                Some(Box::new(&self.config.runtime))
            }
            Some(level) => match level {
                ConfigOrder::Runtime => match &self.config.data {
                    Some(c) => {
                        self.curr = Some(ConfigOrder::Persistent);
                        Some(Box::new(c))
                    }
                    None => None,
                },
                ConfigOrder::Persistent => None,
            },
        }
    }
}

impl Config {
    pub fn new(env: &Environment, build_dir: &Option<String>, runtime: Ffx) -> Result<Self> {
        Ok(Self {
            data: Some(Config::load_persistent_config(env, build_dir)?),
            runtime: Runtime::new(runtime),
        })
    }

    #[cfg(test)]
    fn new_without_perisist_config(runtime: Ffx) -> Result<Self> {
        Ok(Self { data: None, runtime: Runtime::new(runtime) })
    }

    fn iter(&self) -> ReadConfigIterator<'_> {
        ReadConfigIterator { curr: None, config: self }
    }

    fn load_persistent_config(env: &Environment, build_dir: &Option<String>) -> Result<FileBacked> {
        build_dir.as_ref().map_or_else(
            || FileBacked::load(&env.global, &None, &env.user),
            |b| {
                FileBacked::load(&env.global, &env.build.as_ref().and_then(|c| c.get(b)), &env.user)
            },
        )
    }
}

impl fmt::Display for Config {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(
            f,
            "FFX configuration can come from several places and has an inherent priority assigned\n\
            to the different ways the configuration is gathered. A configuration key can be set\n\
            in multiple locations but the first value found is returned. The following output\n\
            shows the locations checked in descending priority order.\n"
        )?;
        self.iter().try_for_each(|c| {
            writeln!(f, "{:*>70}", "")?;
            write!(f, "{}", c)
        })?;
        writeln!(f, "{:*>70}", "")
    }
}

impl PersistentConfig for Config {
    fn save(
        &self,
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<()> {
        match &self.data {
            Some(c) => c.save(global, build, user),
            None => Ok(()),
        }
    }
}

impl ReadConfig for Config {
    fn get(&self, key: &str, mapper: fn(Option<Value>) -> Option<Value>) -> Option<Value> {
        self.iter().find_map(|c| c.get(key, mapper))
    }
}

impl WriteConfig for Config {
    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<()> {
        self.data
            .as_mut()
            .ok_or(anyhow!("configuration not initialized"))
            .and_then(|d| d.set(level, key, value))
    }

    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<()> {
        self.data
            .as_mut()
            .ok_or(anyhow!("configuration not initialized"))
            .and_then(|d| d.remove(level, key))
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use regex::Regex;
    use std::convert::identity;
    use std::default::Default;

    fn test_cli_params(test: &str) -> Ffx {
        Ffx { config: Some(test.to_string()), ..Default::default() }
    }

    #[test]
    fn test_config_runtime() -> Result<()> {
        let (key_1, value_1) = ("test 1", "test 2");
        let (key_2, value_2) = ("test 3", "test 4");

        let config = Config::new_without_perisist_config(test_cli_params(&format!(
            "{}={}, {}={}",
            key_1, value_1, key_2, value_2
        )))?;

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key, identity));
        assert_eq!(Some(Value::String(value_1.to_string())), config.get(key_1, identity));
        assert_eq!(Some(Value::String(value_2.to_string())), config.get(key_2, identity));
        Ok(())
    }

    #[test]
    fn test_config_display() -> Result<()> {
        let (key_1, value_1) = ("run_test_1", "test_1_runtime");
        let (key_2, value_2) = ("run_test_2", "test_2_runtime");
        let config = Config::new_without_perisist_config(test_cli_params(&format!(
            "{}={}, {}={}",
            key_1, value_1, key_2, value_2
        )))?;

        let output = format!("{}", config);

        // Test runtime params
        let run_key_1 = Regex::new(&key_1).expect("test regex");
        assert_eq!(1, run_key_1.find_iter(&output).count());
        let run_key_2 = Regex::new(&key_2).expect("test regex");
        assert_eq!(1, run_key_2.find_iter(&output).count());

        let run_value_1 = Regex::new(&value_1).expect("test regex");
        assert_eq!(1, run_value_1.find_iter(&output).count());
        let run_value_2 = Regex::new(&value_2).expect("test regex");
        assert_eq!(1, run_value_2.find_iter(&output).count());

        Ok(())
    }
}
