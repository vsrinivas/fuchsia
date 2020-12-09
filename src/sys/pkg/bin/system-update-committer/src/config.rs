// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    serde::Deserialize,
    std::{fs::File, io::Read},
    thiserror::Error,
};

/// Static service configuration options.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct Config {
    blobfs: Mode,
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
    // FIXME(http://fxbug.dev/64595) This will be used once that ticket is implemented.
    #[allow(dead_code)]
    pub fn blobfs(&self) -> &Mode {
        &self.blobfs
    }

    pub fn load_from_config_data_or_default() -> Config {
        let f = match File::open("/config/data/config.json") {
            Ok(f) => f,
            Err(e) => {
                fx_log_info!("no config found, using defaults: {:#}", anyhow!(e));
                return Config::default();
            }
        };

        Self::load(f).unwrap_or_else(|e| {
            fx_log_err!("unable to load config, using defaults: {:#}", anyhow!(e));
            Config::default()
        })
    }

    fn load(r: impl Read) -> Result<Config, ConfigLoadError> {
        #[derive(Debug, Deserialize)]
        #[serde(deny_unknown_fields)]
        pub struct ParseConfig {
            #[serde(default = "Mode::default")]
            blobfs: Mode,
        }

        let parse_config = serde_json::from_reader::<_, ParseConfig>(r)?;

        Ok(Config { blobfs: parse_config.blobfs })
    }
}

#[derive(Debug, Error)]
enum ConfigLoadError {
    #[error("parse error")]
    Parse(#[from] serde_json::Error),
}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches, serde_json::json};

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
            verify_load(
                json!({
                    "blobfs": name,
                }),
                Config { blobfs: val.clone() },
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
        assert_eq!(Config::load_from_config_data_or_default(), Config::default());
    }

    #[test]
    fn test_load_empty_is_default() {
        assert_matches!(
            Config::load("{}".as_bytes()),
            Ok(ref config) if config == &Config::default());
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
