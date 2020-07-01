// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::api::ReadConfig, ffx_lib_args::Ffx, serde_json::Value, std::collections::HashMap};

pub(crate) struct Runtime {
    pub runtime_config: HashMap<String, String>,
}

impl Runtime {
    pub(crate) fn new(cli: Ffx) -> Self {
        let mut runtime_config = HashMap::new();
        match cli.config {
            Some(config_str) => config_str.split(',').for_each(|c| {
                let s: Vec<&str> = c.trim().split('=').collect();
                if s.len() == 2 {
                    runtime_config.insert(s[0].to_string(), s[1].to_string());
                }
            }),
            _ => {}
        };
        Self { runtime_config }
    }
}

impl ReadConfig for Runtime {
    fn get(&self, key: &str) -> Option<Value> {
        self.runtime_config.get(key).map(|s| Value::String(s.to_string()))
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use ffx_lib_sub_command::Subcommand;

    fn test_cli_params(test: &str) -> Ffx {
        Ffx {
            target: None,
            config: Some(test.to_string()),
            subcommand: Subcommand::FfxDaemonSuite(ffx_daemon_suite_args::DaemonCommand {
                subcommand: ffx_daemon_suite_sub_command::Subcommand::FfxDaemonStart(
                    ffx_daemon_start_args::StartCommand {},
                ),
            }),
        }
    }

    #[test]
    fn test_config_runtime() {
        let (key_1, value_1) = ("test 1", "test 2");
        let (key_2, value_2) = ("test 3", "test 4");
        let config =
            Runtime::new(test_cli_params(&format!("{}={}, {}={}", key_1, value_1, key_2, value_2)));

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        assert_eq!(Some(Value::String(value_1.to_string())), config.get(key_1));
        assert_eq!(Some(Value::String(value_2.to_string())), config.get(key_2));
    }
}
