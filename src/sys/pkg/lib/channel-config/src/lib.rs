// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use omaha_client::protocol::Cohort;
use serde::{Deserialize, Serialize};
use std::io;

/// Wrapper for deserializing repository configs to the on-disk JSON format.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ChannelConfigs {
    pub default_channel: Option<String>,
    #[serde(rename = "channels")]
    pub known_channels: Vec<ChannelConfig>,
}

impl ChannelConfigs {
    pub fn validate(&self) -> Result<(), io::Error> {
        let names: Vec<&str> = self.known_channels.iter().map(|c| c.name.as_str()).collect();
        if !names.iter().all(|n| Cohort::validate_name(n)) {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "invalid channel name"));
        }
        if let Some(default) = &self.default_channel {
            if !names.contains(&default.as_str()) {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "default channel not a known channel",
                ));
            }
        }
        Ok(())
    }

    pub fn get_default_channel(&self) -> Option<ChannelConfig> {
        self.default_channel.as_ref().and_then(|default| self.get_channel(default))
    }

    pub fn get_channel(&self, name: &str) -> Option<ChannelConfig> {
        self.known_channels.iter().find(|channel_config| channel_config.name == name).cloned()
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ChannelConfig {
    pub name: String,
    pub repo: String,
    pub appid: Option<String>,
    pub check_interval_secs: Option<u64>,
}

impl ChannelConfig {
    pub fn new_for_test(name: &str) -> Self {
        testing::ChannelConfigBuilder::new(name, name.to_owned() + "-repo").build()
    }

    pub fn with_appid_for_test(name: &str, appid: &str) -> Self {
        testing::ChannelConfigBuilder::new(name, name.to_owned() + "-repo").appid(appid).build()
    }
}

pub mod testing {
    use super::*;
    #[derive(Debug, Default)]
    pub struct ChannelConfigBuilder {
        name: String,
        repo: String,
        appid: Option<String>,
        check_interval_secs: Option<u64>,
    }

    impl ChannelConfigBuilder {
        pub fn new(name: impl Into<String>, repo: impl Into<String>) -> Self {
            ChannelConfigBuilder {
                name: name.into(),
                repo: repo.into(),
                ..ChannelConfigBuilder::default()
            }
        }

        pub fn appid(mut self, appid: impl Into<String>) -> Self {
            self.appid = Some(appid.into());
            self
        }

        pub fn check_interval_secs(mut self, check_interval_secs: u64) -> Self {
            self.check_interval_secs = Some(check_interval_secs);
            self
        }

        pub fn build(self) -> ChannelConfig {
            ChannelConfig {
                name: self.name,
                repo: self.repo,
                appid: self.appid,
                check_interval_secs: self.check_interval_secs,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use pretty_assertions::assert_eq;

    #[test]
    fn test_channel_configs_get_default() {
        let configs = ChannelConfigs {
            default_channel: Some("default_channel".to_string()),
            known_channels: vec![
                ChannelConfig::new_for_test("some_channel"),
                ChannelConfig::new_for_test("default_channel"),
                ChannelConfig::new_for_test("other"),
            ],
        };
        assert_eq!(configs.get_default_channel().unwrap(), configs.known_channels[1]);
    }

    #[test]
    fn test_channel_configs_get_default_none() {
        let configs = ChannelConfigs {
            default_channel: None,
            known_channels: vec![ChannelConfig::new_for_test("some_channel")],
        };
        assert_eq!(configs.get_default_channel(), None);
    }

    #[test]
    fn test_channel_configs_get_channel() {
        let configs = ChannelConfigs {
            default_channel: Some("default_channel".to_string()),
            known_channels: vec![
                ChannelConfig::new_for_test("some_channel"),
                ChannelConfig::new_for_test("default_channel"),
                ChannelConfig::new_for_test("other"),
            ],
        };
        assert_eq!(configs.get_channel("other").unwrap(), configs.known_channels[2]);
    }

    #[test]
    fn test_channel_configs_get_channel_missing() {
        let configs = ChannelConfigs {
            default_channel: Some("default_channel".to_string()),
            known_channels: vec![
                ChannelConfig::new_for_test("some_channel"),
                ChannelConfig::new_for_test("default_channel"),
                ChannelConfig::new_for_test("other"),
            ],
        };
        assert_eq!(configs.get_channel("missing"), None);
    }

    #[test]
    fn test_channel_cfg_builder_app_id() {
        let config = testing::ChannelConfigBuilder::new("name", "repo").appid("appid").build();
        assert_eq!("name", config.name);
        assert_eq!("repo", config.repo);
        assert_eq!(Some("appid".to_owned()), config.appid);
        assert_eq!(None, config.check_interval_secs);
    }

    #[test]
    fn test_channel_cfg_builder_check_interval() {
        let config = testing::ChannelConfigBuilder::new("name", "repo")
            .appid("appid")
            .check_interval_secs(3600)
            .build();
        assert_eq!("name", config.name);
        assert_eq!("repo", config.repo);
        assert_eq!(Some("appid".to_owned()), config.appid);
        assert_eq!(Some(3600), config.check_interval_secs);
    }
}
