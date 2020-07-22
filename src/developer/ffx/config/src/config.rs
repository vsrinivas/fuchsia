// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{PersistentConfig, ReadConfig, WriteConfig},
    crate::env_var_config::EnvironmentVariable,
    crate::environment::Environment,
    crate::file_backed_config::FileBacked,
    crate::heuristic_config::{Heuristic, HeuristicFn},
    crate::runtime_config::Runtime,
    anyhow::Error,
    ffx_config_plugin_args::ConfigLevel,
    ffx_lib_args::Ffx,
    serde_json::Value,
    std::collections::HashMap,
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
    type Item = Box<&'a (dyn ReadConfig + 'a)>;

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
    ) -> Result<Self, Error> {
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
    ) -> Result<Self, Error> {
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

    fn load_persistent_config(
        env: &Environment,
        build_dir: &Option<String>,
    ) -> Result<FileBacked, Error> {
        match build_dir {
            Some(b) => {
                FileBacked::load(&env.global, &env.build.as_ref().and_then(|c| c.get(b)), &env.user)
            }
            None => FileBacked::load(&env.global, &None, &env.user),
        }
    }
}

impl<'a> PersistentConfig for Config<'a> {
    fn save(
        &self,
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<(), Error> {
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
    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<(), Error> {
        if self.data.is_some() {
            self.data.as_mut().unwrap().set(level, key, value)
        } else {
            Ok(())
        }
    }

    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<(), Error> {
        if self.data.is_some() {
            self.data.as_mut().unwrap().remove(level, key)
        } else {
            Ok(())
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use ffx_lib_sub_command::Subcommand;

    fn test_heuristic(key: &str) -> Option<Value> {
        Some(Value::String(key.to_string()))
    }

    fn test_cli_params(test: &str) -> Ffx {
        Ffx {
            target: None,
            config: Some(test.to_string()),
            environment_file: None,
            subcommand: Subcommand::FfxDaemonSuite(ffx_daemon_suite_args::DaemonCommand {
                subcommand: ffx_daemon_suite_sub_command::Subcommand::FfxDaemonStart(
                    ffx_daemon_start_args::StartCommand {},
                ),
            }),
        }
    }

    #[test]
    fn test_config_heuristics() -> Result<(), Error> {
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
    fn test_config_environment_variables() -> Result<(), Error> {
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
    fn test_config_runtime() -> Result<(), Error> {
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
    fn test_config_all() -> Result<(), Error> {
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
        Ok(())
    }
}
