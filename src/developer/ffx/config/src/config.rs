// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{PersistentConfig, ReadConfig, ReadDisplayConfig, WriteConfig},
    crate::env_var_config::EnvironmentVariable,
    crate::environment::Environment,
    crate::file_backed_config::FileBacked,
    crate::heuristic_config::{Heuristic, HeuristicFn},
    crate::runtime_config::Runtime,
    anyhow::{anyhow, Result},
    ffx_config_plugin_args::ConfigLevel,
    ffx_lib_args::Ffx,
    serde_json::Value,
    std::{collections::HashMap, fmt},
};

pub struct Config<'a> {
    data: Option<FileBacked>,
    environment_variables: EnvironmentVariable<'a>,
    heuristics: Heuristic<'a>,
    runtime: Runtime,
}

#[derive(Debug, PartialEq, Copy, Clone)]
enum ConfigOrder {
    EnvironmentVariables,
    Heuristics,
    Persistent,
    Runtime,
}

struct ReadConfigIterator<'a> {
    curr: Option<ConfigOrder>,
    config: &'a Config<'a>,
}

impl<'a> Iterator for ReadConfigIterator<'a> {
    type Item = Box<&'a (dyn ReadDisplayConfig + 'a)>;

    fn next(&mut self) -> Option<Self::Item> {
        // Check for configuration in this order:
        // 1. Runtime Configuration (set by the command line)
        // 2. Environment Variables
        // 3. Configuration Files
        // 4. Heuristics (Methods that can guess from the environment)
        match &self.curr {
            None => {
                self.curr = Some(ConfigOrder::Runtime);
                Some(Box::new(&self.config.runtime))
            }
            Some(level) => match level {
                ConfigOrder::Runtime => {
                    self.curr = Some(ConfigOrder::EnvironmentVariables);
                    Some(Box::new(&self.config.environment_variables))
                }
                ConfigOrder::EnvironmentVariables => match &self.config.data {
                    Some(c) => {
                        self.curr = Some(ConfigOrder::Persistent);
                        Some(Box::new(c))
                    }
                    None => {
                        self.curr = Some(ConfigOrder::Heuristics);
                        Some(Box::new(&self.config.heuristics))
                    }
                },
                ConfigOrder::Persistent => {
                    self.curr = Some(ConfigOrder::Heuristics);
                    Some(Box::new(&self.config.heuristics))
                }
                ConfigOrder::Heuristics => None,
            },
        }
    }
}

impl<'a> Config<'a> {
    pub fn new(
        env: &Environment,
        build_dir: &Option<String>,
        environment_variables: &'a HashMap<&'static str, Vec<&'static str>>,
        heuristics: &'a HashMap<&'static str, HeuristicFn>,
        runtime: Ffx,
    ) -> Result<Self> {
        Ok(Self {
            data: Some(Config::load_persistent_config(env, build_dir)?),
            environment_variables: EnvironmentVariable::new(environment_variables),
            heuristics: Heuristic::new(heuristics),
            runtime: Runtime::new(runtime),
        })
    }

    #[cfg(test)]
    fn new_without_perisist_config(
        environment_variables: &'a HashMap<&'static str, Vec<&'static str>>,
        heuristics: &'a HashMap<&'static str, HeuristicFn>,
        runtime: Ffx,
    ) -> Result<Self> {
        Ok(Self {
            data: None,
            environment_variables: EnvironmentVariable::new(environment_variables),
            heuristics: Heuristic::new(heuristics),
            runtime: Runtime::new(runtime),
        })
    }

    fn iter(&'a self) -> ReadConfigIterator<'a> {
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

impl<'a> fmt::Display for Config<'a> {
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

impl<'a> PersistentConfig for Config<'a> {
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

impl<'a> ReadConfig for Config<'a> {
    fn get(&self, key: &str) -> Option<Value> {
        self.iter().find_map(|c| c.get(key))
    }
}

impl<'a> WriteConfig for Config<'a> {
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
    use std::default::Default;

    fn test_heuristic(key: &str) -> Option<Value> {
        Some(Value::String(key.to_string()))
    }

    fn test_cli_params(test: &str) -> Ffx {
        Ffx { config: Some(test.to_string()), ..Default::default() }
    }

    #[test]
    fn test_config_heuristics() -> Result<()> {
        let (heuristic_key, heuristic_key_2) = ("test", "test_2");
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(heuristic_key, test_heuristic);
        heuristics.insert(heuristic_key_2, test_heuristic);

        let environment_variables = HashMap::new();
        let config = Config::new_without_perisist_config(
            &environment_variables,
            &heuristics,
            test_cli_params(""),
        )?;

        let missing_key = "whatever";

        assert_eq!(None, config.get(missing_key));
        assert_eq!(Some(Value::String(heuristic_key.to_string())), config.get(heuristic_key));
        assert_eq!(Some(Value::String(heuristic_key_2.to_string())), config.get(heuristic_key_2));
        Ok(())
    }

    #[test]
    fn test_config_environment_variables() -> Result<()> {
        let (env_key, env_key_2) = ("test", "test_2");
        let (env_var_1, env_var_1_value) = ("FFX_TEST_1", "test 1");
        let (env_var_2, env_var_2_value) = ("FFX_TEST_2", "test 2");
        let (env_var_3, env_var_3_value) = ("FFX_TEST_3", "test 3");
        let (env_var_4, env_var_4_value) = ("FFX_TEST_4", "test 4");
        vec![env_var_1, env_var_2, env_var_3, env_var_4].iter().for_each(std::env::remove_var);

        let mut environment_variables = HashMap::<&str, Vec<&str>>::new();
        environment_variables.insert(env_key, vec![env_var_1, env_var_2, env_var_3]);
        environment_variables.insert(env_key_2, vec![env_var_4]);

        let heuristics = HashMap::new();

        let config = Config::new_without_perisist_config(
            &environment_variables,
            &heuristics,
            test_cli_params(""),
        )?;

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        assert_eq!(None, config.get(env_key));
        assert_eq!(None, config.get(env_key_2));

        std::env::set_var(env_var_4, env_var_4_value);
        assert_eq!(Some(Value::String(env_var_4_value.to_string())), config.get(env_key_2));

        std::env::set_var(env_var_3, env_var_3_value);
        assert_eq!(Some(Value::String(env_var_3_value.to_string())), config.get(env_key));
        std::env::set_var(env_var_2, env_var_2_value);
        assert_eq!(Some(Value::String(env_var_2_value.to_string())), config.get(env_key));
        std::env::set_var(env_var_1, env_var_1_value);
        assert_eq!(Some(Value::String(env_var_1_value.to_string())), config.get(env_key));
        Ok(())
    }

    #[test]
    fn test_config_runtime() -> Result<()> {
        let (key_1, value_1) = ("test 1", "test 2");
        let (key_2, value_2) = ("test 3", "test 4");

        let environment_variables = HashMap::new();
        let heuristics = HashMap::new();

        let config = Config::new_without_perisist_config(
            &environment_variables,
            &heuristics,
            test_cli_params(&format!("{}={}, {}={}", key_1, value_1, key_2, value_2)),
        )?;

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        assert_eq!(Some(Value::String(value_1.to_string())), config.get(key_1));
        assert_eq!(Some(Value::String(value_2.to_string())), config.get(key_2));
        Ok(())
    }

    #[test]
    fn test_config_all() -> Result<()> {
        let (heuristic_key, heuristic_key_2) = ("test", "test_2");
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(heuristic_key, test_heuristic);
        heuristics.insert(heuristic_key_2, test_heuristic);

        let (env_key, env_key_2) = ("test", "test_2");
        let (env_var_1, env_var_1_value) = ("FFX_ALL_TEST_1", "test 1");
        let (env_var_2, env_var_2_value) = ("FFX_ALL_TEST_2", "test 2");
        let (env_var_3, env_var_3_value) = ("FFX_ALL_TEST_3", "test 3");
        let (env_var_4, env_var_4_value) = ("FFX_ALL_TEST_4", "test 4");
        vec![env_var_1, env_var_2, env_var_3, env_var_4].iter().for_each(std::env::remove_var);

        let mut environment_variables = HashMap::<&str, Vec<&str>>::new();
        environment_variables.insert(env_key, vec![env_var_1, env_var_2, env_var_3]);
        environment_variables.insert(env_key_2, vec![env_var_4]);

        let (key_1, value_1) = ("test_1", "test_1_runtime");
        let (key_2, value_2) = ("test_2", "test_2_runtime");
        let config = Config::new_without_perisist_config(
            &environment_variables,
            &heuristics,
            test_cli_params(&format!("{}={}, {}={}", key_1, value_1, key_2, value_2)),
        )?;

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        assert_eq!(Some(Value::String(value_1.to_string())), config.get(key_1));
        assert_eq!(Some(Value::String(value_2.to_string())), config.get(key_2));
        assert_eq!(Some(Value::String(heuristic_key.to_string())), config.get(heuristic_key));
        assert_eq!(Some(Value::String(value_2.to_string())), config.get(heuristic_key_2));

        std::env::set_var(env_var_4, env_var_4_value);
        assert_eq!(Some(Value::String(value_2.to_string())), config.get(env_key_2));
        std::env::set_var(env_var_3, env_var_3_value);
        assert_eq!(Some(Value::String(env_var_3_value.to_string())), config.get(env_key));
        std::env::set_var(env_var_2, env_var_2_value);
        assert_eq!(Some(Value::String(env_var_2_value.to_string())), config.get(env_key));
        std::env::set_var(env_var_1, env_var_1_value);
        assert_eq!(Some(Value::String(env_var_1_value.to_string())), config.get(env_key));

        // Cleanup
        vec![env_var_1, env_var_2, env_var_3, env_var_4].iter().for_each(std::env::remove_var);

        Ok(())
    }

    #[test]
    fn test_config_display() -> Result<()> {
        let (heuristic_key, heuristic_key_2) = ("h_test", "h_test_2");
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(heuristic_key, test_heuristic);
        heuristics.insert(heuristic_key_2, test_heuristic);

        let (env_key, env_key_2) = ("env_test", "env_test_2");
        let (env_var_1, env_var_1_value) = ("FFX_DISPLAY_TEST_1", "env test 1");
        let (env_var_2, env_var_2_value) = ("FFX_DISPLAY_TEST_2", "env test 2");
        let (env_var_3, env_var_3_value) = ("FFX_DISPLAY_TEST_3", "env test 3");
        let (env_var_4, env_var_4_value) = ("FFX_DISPLAY_TEST_4", "env test 4");
        vec![env_var_1, env_var_2, env_var_3, env_var_4].iter().for_each(std::env::remove_var);
        vec![
            (env_var_1, env_var_1_value),
            (env_var_2, env_var_2_value),
            (env_var_3, env_var_3_value),
            (env_var_4, env_var_4_value),
        ]
        .iter()
        .for_each(|(key, value)| {
            std::env::set_var(key, value);
        });

        let mut environment_variables = HashMap::<&str, Vec<&str>>::new();
        environment_variables.insert(env_key, vec![env_var_1, env_var_2, env_var_3]);
        environment_variables.insert(env_key_2, vec![env_var_4]);

        let (key_1, value_1) = ("run_test_1", "test_1_runtime");
        let (key_2, value_2) = ("run_test_2", "test_2_runtime");
        let config = Config::new_without_perisist_config(
            &environment_variables,
            &heuristics,
            test_cli_params(&format!("{}={}, {}={}", key_1, value_1, key_2, value_2)),
        )?;

        let output = format!("{}", config);
        let h_test_1 = format!("\"{}\" = \"{}\"", heuristic_key, heuristic_key);
        let h_reg_1 = Regex::new(&h_test_1).expect("test regex");
        assert_eq!(1, h_reg_1.find_iter(&output).count(), "{}", output);
        let h_test_2 = format!("\"{}\" = \"{}\"", heuristic_key_2, heuristic_key_2);
        let h_reg_2 = Regex::new(&h_test_2).expect("test regex");
        assert_eq!(1, h_reg_2.find_iter(&output).count());

        let env_test_1 = format!("\"{}\" = \"{}\"", env_key, env_var_1_value);
        let env_key_1_reg = Regex::new(&env_test_1).expect("test regex");
        assert_eq!(1, env_key_1_reg.find_iter(&output).count(), "{}", output);
        let env_test_2 = format!("\"{}\" = \"{}\"", env_key_2, env_var_4_value);
        let env_key_2_reg = Regex::new(&env_test_2).expect("test regex");
        assert_eq!(1, env_key_2_reg.find_iter(&output).count());

        // Test environment variables explained.
        let env_var_1_reg = Regex::new(&env_var_1).expect("test regex");
        assert_eq!(1, env_var_1_reg.find_iter(&output).count());
        let env_var_2_reg = Regex::new(&env_var_2).expect("test regex");
        assert_eq!(1, env_var_2_reg.find_iter(&output).count());
        let env_var_3_reg = Regex::new(&env_var_3).expect("test regex");
        assert_eq!(1, env_var_3_reg.find_iter(&output).count());
        let env_var_4_reg = Regex::new(&env_var_4).expect("test regex");
        assert_eq!(1, env_var_4_reg.find_iter(&output).count());

        // Test runtime params
        let run_key_1 = Regex::new(&key_1).expect("test regex");
        assert_eq!(1, run_key_1.find_iter(&output).count());
        let run_key_2 = Regex::new(&key_2).expect("test regex");
        assert_eq!(1, run_key_2.find_iter(&output).count());

        let run_value_1 = Regex::new(&value_1).expect("test regex");
        assert_eq!(1, run_value_1.find_iter(&output).count());
        let run_value_2 = Regex::new(&value_2).expect("test regex");
        assert_eq!(1, run_value_2.find_iter(&output).count());

        // Cleanup
        vec![env_var_1, env_var_2, env_var_3, env_var_4].iter().for_each(std::env::remove_var);

        Ok(())
    }
}
