// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    serde::Deserialize,
    std::{
        fs::File,
        io::{BufReader, Read},
    },
    thiserror::Error,
    tracing::{error, info},
};

/// Static service configuration options.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct Config {
    enable_dynamic_configuration: bool,
    persisted_repos_dir: String,
}

impl Config {
    pub fn enable_dynamic_configuration(&self) -> bool {
        self.enable_dynamic_configuration
    }

    pub fn persisted_repos_dir(&self) -> Option<&str> {
        match self.persisted_repos_dir.as_str() {
            "" => None,
            _ => Some(&self.persisted_repos_dir),
        }
    }

    pub fn load_from_config_data_or_default() -> Config {
        let dynamic_config = match File::open("/config/data/config.json") {
            Ok(f) => Self::load_enable_dynamic_config(BufReader::new(f)).unwrap_or_else(|e| {
                error!("unable to load config, using defaults: {:#}", anyhow!(e));
                Config::default()
            }),
            Err(e) => {
                info!("no config found, using defaults: {:#}", anyhow!(e));
                Config::default()
            }
        };

        let repo_config = match File::open("/config/data/persisted_repos_dir.json") {
            Ok(f) => Self::load_persisted_repos_config(BufReader::new(f)).unwrap_or_else(|e| {
                error!("unable to load config, using defaults: {:#}", anyhow!(e));
                Config::default()
            }),
            Err(e) => {
                info!("no config found, using defaults: {:#}", anyhow!(e));
                Config::default()
            }
        };

        Config {
            enable_dynamic_configuration: dynamic_config.enable_dynamic_configuration,
            persisted_repos_dir: repo_config.persisted_repos_dir,
        }
    }

    fn load_enable_dynamic_config(r: impl Read) -> Result<Config, ConfigLoadError> {
        #[derive(Debug, Deserialize)]
        #[serde(deny_unknown_fields)]
        struct ParseConfig {
            enable_dynamic_configuration: bool,
        }

        let parse_config = serde_json::from_reader::<_, ParseConfig>(r)?;

        Ok(Config {
            enable_dynamic_configuration: parse_config.enable_dynamic_configuration,
            ..Default::default()
        })
    }

    fn load_persisted_repos_config(r: impl Read) -> Result<Config, ConfigLoadError> {
        #[derive(Debug, Deserialize)]
        #[serde(deny_unknown_fields)]
        struct ParseConfig {
            persisted_repos_dir: String,
        }

        let parse_config = serde_json::from_reader::<_, ParseConfig>(r)?;

        Ok(Config { persisted_repos_dir: parse_config.persisted_repos_dir, ..Default::default() })
    }
}

#[derive(Debug, Error)]
enum ConfigLoadError {
    #[error("parse error")]
    Parse(#[from] serde_json::Error),
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, serde_json::json};

    fn verify_load_dyn(input: serde_json::Value, expected: Config) {
        assert_eq!(
            Config::load_enable_dynamic_config(input.to_string().as_bytes())
                .expect("json value to be valid"),
            expected
        );
    }

    fn verify_load_repo(input: serde_json::Value, expected: Config) {
        assert_eq!(
            Config::load_persisted_repos_config(input.to_string().as_bytes())
                .expect("json value to be valid"),
            expected
        );
    }

    #[test]
    fn test_load_valid_configs() {
        for val in [true, false].iter() {
            verify_load_dyn(
                json!({
                    "enable_dynamic_configuration": *val,
                }),
                Config { enable_dynamic_configuration: *val, ..Default::default() },
            );
        }

        verify_load_repo(
            json!({
                "persisted_repos_dir": "boo",
            }),
            Config { persisted_repos_dir: "boo".to_string(), ..Default::default() },
        )
    }

    #[test]
    fn test_load_errors_on_unknown_field() {
        assert_matches!(
            Config::load_enable_dynamic_config(
                json!({
                    "enable_dynamic_configuration": false,
                    "unknown_field": 3
                })
                .to_string()
                .as_bytes()
            ),
            Err(ConfigLoadError::Parse(_))
        );
        assert_matches!(
            Config::load_persisted_repos_config(
                json!({
                    "persisted_repos_dir": "boo".to_string(),
                    "unknown_field": 3
                })
                .to_string()
                .as_bytes()
            ),
            Err(ConfigLoadError::Parse(_))
        );
    }

    #[test]
    fn test_no_config_data_is_default() {
        assert_eq!(Config::load_from_config_data_or_default(), Config::default());
    }

    #[test]
    fn test_default_disables_dynamic_configuration() {
        assert_eq!(Config::default().enable_dynamic_configuration, false);
    }

    #[test]
    fn test_default_disables_persisted_repos() {
        assert_eq!(Config::default().persisted_repos_dir(), None);
    }
}
