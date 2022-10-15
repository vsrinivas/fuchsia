// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    serde::Deserialize,
    std::{fs::File, io::Read},
    thiserror::Error,
    tracing::{error, info},
    typed_builder::TypedBuilder,
};

/// Static service configuration options.
#[derive(Debug, PartialEq, Eq, TypedBuilder)]
pub struct Config {
    #[builder(default)]
    blobfs: Mode,

    #[builder(default)]
    #[allow(dead_code)]
    // TODO(https://fxbug.dev/76636): Make use of this config type.
    netstack: Mode,

    #[builder(default = true)]
    enable: bool,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Mode {
    Ignore,
    RebootOnFailure,
}

impl Default for Mode {
    fn default() -> Self {
        Mode::Ignore
    }
}

impl Config {
    pub fn blobfs(&self) -> &Mode {
        &self.blobfs
    }

    // TODO(https://fxbug.dev/76636): Make use of this method.
    #[allow(dead_code)]
    pub fn netstack(&self) -> &Mode {
        &self.netstack
    }

    pub fn enable(&self) -> bool {
        self.enable
    }

    pub fn load_from_config_data_or_default() -> Config {
        let f = match File::open("/config/data/config.json") {
            Ok(f) => f,
            Err(e) => {
                info!("no config found, using defaults: {:#}", anyhow!(e));
                return Config::builder().build();
            }
        };

        Self::load(f).unwrap_or_else(|e| {
            error!("unable to load config, using defaults: {:#}", anyhow!(e));
            Config::builder().build()
        })
    }

    fn enable_default() -> bool {
        true
    }

    fn load(r: impl Read) -> Result<Config, ConfigLoadError> {
        #[derive(Debug, Deserialize)]
        #[serde(deny_unknown_fields)]
        pub struct ParseConfig {
            #[serde(default = "Mode::default")]
            blobfs: Mode,
            #[serde(default = "Mode::default")]
            netstack: Mode,
            #[serde(default = "Config::enable_default")]
            enable: bool,
        }

        let parse_config = serde_json::from_reader::<_, ParseConfig>(r)?;

        Ok(Config {
            blobfs: parse_config.blobfs,
            netstack: parse_config.netstack,
            enable: parse_config.enable,
        })
    }
}

#[derive(Debug, Error)]
enum ConfigLoadError {
    #[error("parse error")]
    Parse(#[from] serde_json::Error),
}

#[cfg(test)]
pub(crate) mod tests {

    use {super::*, assert_matches::assert_matches, serde_json::json};

    fn verify_load(input: serde_json::Value, expected: Config) {
        assert_eq!(
            Config::load(input.to_string().as_bytes()).expect("json value to be valid"),
            expected
        );
    }

    #[test]
    fn test_load_valid_configs() {
        for (name, val) in
            [("ignore", Mode::Ignore), ("reboot_on_failure", Mode::RebootOnFailure)].iter()
        {
            // Verify that setting enable explicitly works...
            verify_load(
                json!({
                    "blobfs": name,
                    "netstack": name,
                    "enable": false,
                }),
                Config::builder().blobfs(val.clone()).netstack(val.clone()).enable(false).build(),
            );
            // ... and that leaving it unset defaults to true.
            verify_load(
                json!({
                    "blobfs": name,
                    "netstack": name,
                }),
                Config::builder().blobfs(val.clone()).netstack(val.clone()).enable(true).build(),
            );
        }
    }

    #[test]
    fn test_load_errors_on_unknown_field() {
        assert_matches!(
            Config::load(
                json!({
                    "blofs": "ignore",
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
        assert_eq!(Config::load_from_config_data_or_default(), Config::builder().build());
    }

    #[test]
    fn test_load_empty_is_default() {
        assert_matches!(
            Config::load("{}".as_bytes()),
            Ok(ref config) if config == &Config::builder().build());
    }

    #[test]
    fn test_load_rejects_invalid_json() {
        assert_matches!(
            Config::load("not json".as_bytes()),
            Err(ConfigLoadError::Parse(ref err)) if err.is_syntax());
    }

    #[test]
    fn test_load_rejects_invalid_mode() {
        let input = json!({
            "blobfs": "invalid-config-option",
        })
        .to_string();

        assert_matches!(
            Config::load(input.as_bytes()),
            Err(ConfigLoadError::Parse(ref err)) if err.is_data());
    }
}
