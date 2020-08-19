// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use omaha_client::protocol::Cohort;
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;

const CHANNEL_CONFIG_PATH: &str = "/config/data/channel_config.json";

/// The `channel` module contains the implementation for reading the `channel_config.json`
/// configuration file.

/// Wrapper for deserializing channel configurations to the on-disk JSON format.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum ChannelConfigFormats {
    #[serde(rename = "1")]
    Version1(ChannelConfigs),
}

/// Wrapper for deserializing repository configs to the on-disk JSON format.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub struct ChannelConfigs {
    pub default_channel: Option<String>,
    #[serde(rename = "channels")]
    pub known_channels: Vec<ChannelConfig>,
}

impl ChannelConfigs {
    fn validate(self) -> Result<Self, io::Error> {
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
        Ok(self)
    }

    pub fn get_default_channel(&self) -> Option<ChannelConfig> {
        self.default_channel.as_ref().and_then(|default| self.get_channel(&default))
    }

    pub fn get_channel(&self, name: &str) -> Option<ChannelConfig> {
        self.known_channels
            .iter()
            .find(|channel_config| channel_config.name == name)
            .map(|c| c.clone())
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ChannelConfig {
    pub name: String,
    pub repo: String,
    pub appid: Option<String>,
    pub check_interval_secs: Option<u64>,
}

#[cfg(test)]
impl ChannelConfig {
    pub fn new(name: &str) -> Self {
        ChannelConfigBuilder::new(name, name.to_owned() + "-repo").build()
    }

    pub fn with_appid(name: &str, appid: &str) -> Self {
        ChannelConfigBuilder::new(name, name.to_owned() + "-repo").appid(appid).build()
    }
}

#[cfg(test)]
#[derive(Debug, Default)]
pub struct ChannelConfigBuilder {
    name: String,
    repo: String,
    appid: Option<String>,
    check_interval_secs: Option<u64>,
}

#[cfg(test)]
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

/// This method retrieves the channel configuation from the file in config data
pub fn get_configs() -> Result<ChannelConfigs, io::Error> {
    get_configs_from_path(CHANNEL_CONFIG_PATH)
}

// This method is inserted to create a point for ease of testing the behavior
// when the file is not present.
fn get_configs_from_path(path: &str) -> Result<ChannelConfigs, io::Error> {
    let file = fs::File::open(path)?;
    get_configs_from(io::BufReader::new(file))
}

// This method does the actual work of deserializing the configuration and
// validating that the data is useable.
fn get_configs_from<R>(reader: R) -> Result<ChannelConfigs, io::Error>
where
    R: io::Read,
{
    let config_format = serde_json::from_reader(reader)?;
    match config_format {
        ChannelConfigFormats::Version1(configs) => configs.validate(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_update_channelcontrol::ChannelControlMarker;
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_service;
    use pretty_assertions::assert_eq;

    #[fasync::run_singlethreaded(test)]
    async fn test_fake_channel_control() {
        let control = connect_to_service::<ChannelControlMarker>().unwrap();

        control.set_target("test-target-channel").await.unwrap();
        assert_eq!("test-target-channel", control.get_target().await.unwrap());
        assert_eq!("fake-current-channel", control.get_current().await.unwrap());

        control.set_target("test-target-channel-2").await.unwrap();
        assert_eq!("test-target-channel-2", control.get_target().await.unwrap());
        assert_eq!("fake-current-channel", control.get_current().await.unwrap());
    }

    // This test uses test data in "::omaha_client_channels_test_config"
    #[test]
    fn test_get_configs() {
        let configs = get_configs().unwrap();

        assert_eq!(Some("some-other-channel".to_string()), configs.default_channel);
        assert_eq!("some-channel", configs.known_channels[0].name);
        assert_eq!("some-channel-repo", configs.known_channels[0].repo);
        assert_eq!(Some("some-channel-appid".to_string()), configs.known_channels[0].appid);
        assert_eq!("some-other-channel", configs.known_channels[1].name);
        assert_eq!("some-other-channel-repo", configs.known_channels[1].repo);
        assert_eq!(Some("some-other-channel-appid".to_string()), configs.known_channels[1].appid);
    }

    #[test]
    fn test_channel_configs_no_default() {
        let json = r#"
{
  "version": "1",
  "content": {
    "channels": [
      {
        "name": "a-channel",
        "repo": "a-channel-repo"
      },
      {
        "name": "another-channel",
        "repo": "another-channel-repo"
      }
    ]
  }
}"#;
        let config = get_configs_from(json.as_bytes()).unwrap();
        assert_eq!(None, config.default_channel);
    }

    #[test]
    fn test_channel_configs_invalid_name() {
        let json = r#"
{
  "version": "1",
  "content": {
    "channels": [
      {
        "name": "an-invalid-char-\u{010}-channel",
        "repo": "a-channel-repo"
      }
    ]
  }
}"#;
        let config = get_configs_from(json.as_bytes());
        assert!(config.is_err());
    }

    #[test]
    fn test_channel_configs_get_default() {
        let configs = ChannelConfigs {
            default_channel: Some("default_channel".to_string()),
            known_channels: vec![
                ChannelConfig::new("some_channel"),
                ChannelConfig::new("default_channel"),
                ChannelConfig::new("other"),
            ],
        };
        assert_eq!(configs.get_default_channel().unwrap(), configs.known_channels[1]);
    }

    #[test]
    fn test_channel_configs_get_default_none() {
        let configs = ChannelConfigs {
            default_channel: None,
            known_channels: vec![ChannelConfig::new("some_channel")],
        };
        assert_eq!(configs.get_default_channel(), None);
    }

    #[test]
    fn test_channel_configs_get_channel() {
        let configs = ChannelConfigs {
            default_channel: Some("default_channel".to_string()),
            known_channels: vec![
                ChannelConfig::new("some_channel"),
                ChannelConfig::new("default_channel"),
                ChannelConfig::new("other"),
            ],
        };
        assert_eq!(configs.get_channel("other").unwrap(), configs.known_channels[2]);
    }

    #[test]
    fn test_channel_configs_get_channel_missing() {
        let configs = ChannelConfigs {
            default_channel: Some("default_channel".to_string()),
            known_channels: vec![
                ChannelConfig::new("some_channel"),
                ChannelConfig::new("default_channel"),
                ChannelConfig::new("other"),
            ],
        };
        assert_eq!(configs.get_channel("missing"), None);
    }

    #[test]
    fn test_missing_channel_configs_file_behavior() {
        let config = get_configs_from_path("/config/data/invalid.path");
        assert!(config.is_err());
    }

    #[test]
    fn test_channel_cfg_builder_app_id() {
        let config = ChannelConfigBuilder::new("name", "repo").appid("appid").build();
        assert_eq!("name", config.name);
        assert_eq!("repo", config.repo);
        assert_eq!(Some("appid".to_owned()), config.appid);
        assert_eq!(None, config.check_interval_secs);
    }

    #[test]
    fn test_channel_cfg_builder_check_interval() {
        let config = ChannelConfigBuilder::new("name", "repo")
            .appid("appid")
            .check_interval_secs(3600)
            .build();
        assert_eq!("name", config.name);
        assert_eq!("repo", config.repo);
        assert_eq!(Some("appid".to_owned()), config.appid);
        assert_eq!(Some(3600), config.check_interval_secs);
    }
}
