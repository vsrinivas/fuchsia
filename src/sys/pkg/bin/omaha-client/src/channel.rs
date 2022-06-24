// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use channel_config::ChannelConfigs;
use serde::Deserialize;
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
    let ChannelConfigFormats::Version1(configs) = serde_json::from_reader(reader)?;
    let () = configs.validate()?;
    Ok(configs)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_update_channelcontrol::ChannelControlMarker;
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_protocol;
    use pretty_assertions::assert_eq;

    #[fasync::run_singlethreaded(test)]
    async fn test_fake_channel_control() {
        let control = connect_to_protocol::<ChannelControlMarker>().unwrap();

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
    fn test_missing_channel_configs_file_behavior() {
        let config = get_configs_from_path("/config/data/invalid.path");
        assert!(config.is_err());
    }
}
