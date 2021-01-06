// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl_fuchsia_bluetooth_bredr as bredr,
    serde::{self, Deserialize},
    std::{collections::HashSet, fs::File, io::Read},
    thiserror::Error,
};

use crate::sources::AudioSourceType;

pub const DEFAULT_CONFIG_FILE_PATH: &str = "/config/data/a2dp.config";

pub(crate) const DEFAULT_DOMAIN: &str = "Bluetooth";

#[derive(FromArgs)]
#[argh(description = "Bluetooth Advanced Audio Distribution Profile")]
pub struct A2dpConfigurationArgs {
    /// published media session domain (optional, defaults to 'Bluetooth')
    #[argh(option)]
    pub domain: Option<String>,
    #[argh(option)]
    /// audio source for A2DP source streams. options: [audio_out, big_ben], Defaults to 'audio_out'
    /// has no effect if source is disabled
    pub source: Option<AudioSourceType>,
    /// channel mode requested for the signaling channel
    /// options: [basic, etrm]. Defaults to 'basic'
    #[argh(option, short = 'c', long = "channelmode")]
    pub channel_mode: Option<String>,

    /// enable source, allowing peers to stream audio to this device. defaults to true.
    #[argh(option)]
    pub enable_source: Option<bool>,
    /// enable sink, allowing peers to stream audio from this device. defaults to true.
    #[argh(option)]
    pub enable_sink: Option<bool>,
}

/// Parses the ChannelMode from the String argument.
///
/// Returns an Error if the provided argument is an invalid string.
fn channel_mode_from_str(channel_mode: String) -> Result<bredr::ChannelMode, Error> {
    match channel_mode.as_str() {
        "basic" => Ok(bredr::ChannelMode::Basic),
        "ertm" => Ok(bredr::ChannelMode::EnhancedRetransmission),
        s => return Err(format_err!("invalid channel mode: {}", s)),
    }
}

fn deserialize_channel_mode<'de, D>(deserializer: D) -> Result<bredr::ChannelMode, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let str = String::deserialize(deserializer)?;

    channel_mode_from_str(str).map_err(serde::de::Error::custom)
}

/// Configuration parameters for A2DP.
/// Typically loaded from a config file provided during build.
/// See [`A2dpConfiguration::load_default`]
#[derive(Deserialize, Clone, Debug)]
#[cfg_attr(test, derive(PartialEq))]
#[serde(deny_unknown_fields, default)]
pub struct A2dpConfiguration {
    /// The media session domain which is reported to the Fuchsia media system.
    pub domain: String,
    /// The source for audio sent to sinks connected to this profile.
    pub source: AudioSourceType,
    /// Mode used for A2DP signaling channel establishment.
    #[serde(deserialize_with = "deserialize_channel_mode")]
    pub channel_mode: bredr::ChannelMode,
    /// Enable source streams. defaults to true.
    pub enable_source: bool,
    /// Enable sink streams. defaults to true.
    pub enable_sink: bool,
}

impl Default for A2dpConfiguration {
    fn default() -> Self {
        A2dpConfiguration {
            domain: DEFAULT_DOMAIN.into(),
            source: AudioSourceType::AudioOut,
            channel_mode: bredr::ChannelMode::Basic,
            enable_source: true,
            enable_sink: true,
        }
    }
}

/// Problems that can exist with a configuration not covered by syntax errors.
#[derive(Error, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum ConfigurationError {
    #[error("Must enable at least one of source or sink")]
    NoProfilesEnabled,
}

impl A2dpConfiguration {
    /// Loads configuration using the default method
    /// The configuration file is used if it exists, with runtime arguments overriding them if
    /// present.  Returns Error if there is either syntax or configuration errors.
    pub fn load_default() -> Result<Self, Error> {
        let configured = Self::from_config(DEFAULT_CONFIG_FILE_PATH).unwrap_or(Default::default());
        let args: A2dpConfigurationArgs = argh::from_env();
        let merged = configured.merge(args)?;
        let problems = merged.errors();
        if !problems.is_empty() {
            return Err(format_err!("Configuration unsupported: {:?}", problems));
        }
        Ok(merged)
    }

    pub fn merge(self, args: A2dpConfigurationArgs) -> Result<Self, Error> {
        let channel_mode = match args.channel_mode {
            Some(s) => channel_mode_from_str(s)?,
            None => self.channel_mode,
        };
        Ok(Self {
            domain: args.domain.unwrap_or(self.domain),
            source: args.source.unwrap_or(self.source),
            enable_source: args.enable_source.unwrap_or(self.enable_source),
            enable_sink: args.enable_sink.unwrap_or(self.enable_sink),
            channel_mode,
            ..self
        })
    }

    pub fn from_config(path: &str) -> Result<Self, Error> {
        Self::from_reader(File::open(path)?)
    }

    pub fn from_reader<R: Read>(config_reader: R) -> Result<Self, Error> {
        Ok(serde_json::from_reader(config_reader)?)
    }

    /// Returns a set of configuration problems with the current configuration.
    pub fn errors(&self) -> HashSet<ConfigurationError> {
        let mut e = HashSet::new();
        if !(self.enable_sink || self.enable_source) {
            e.insert(ConfigurationError::NoProfilesEnabled);
        }
        e
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;
    #[test]
    fn test_channel_mode_from_str() {
        let channel_string = "basic".to_string();
        assert_matches!(channel_mode_from_str(channel_string), Ok(bredr::ChannelMode::Basic));

        let channel_string = "ertm".to_string();
        assert_matches!(
            channel_mode_from_str(channel_string),
            Ok(bredr::ChannelMode::EnhancedRetransmission)
        );

        let channel_string = "foobar123".to_string();
        assert!(channel_mode_from_str(channel_string).is_err());
    }

    #[test]
    fn success_using_provided_config_file() {
        A2dpConfiguration::load_default().expect("provided config is not Ok()");
    }

    #[test]
    fn failure_malformed_config_data() {
        let invalid_json = br#"
        {
            "domain" :
        }"#;
        assert!(A2dpConfiguration::from_reader(&invalid_json[..]).is_err());

        let unknown_fields = br#"
            {
                "some_unknown_field": true,
                "domain": "Testing",
                "source": "audio_out",
                "channel_mode": "ertm"
            }
        "#;
        assert!(A2dpConfiguration::from_reader(&unknown_fields[..]).is_err());

        let incorrectly_typed_fields = br#"
            {
                "domain": false,
                "source": 2,
                "channel_mode": 0.1
            }
        "#;
        assert!(A2dpConfiguration::from_reader(&incorrectly_typed_fields[..]).is_err());
    }

    #[test]
    fn unsupported_configs() {
        let no_profiles = br#"
            {
                "enable_source": false,
                "enable_sink": false
            }
        "#;
        let config = A2dpConfiguration::from_reader(&no_profiles[..]).expect("no syntax errors");

        let problems = config.errors();
        assert!(problems.contains(&ConfigurationError::NoProfilesEnabled));
    }

    #[test]
    fn missing_field_defaults() {
        let missing_domain = br#"
            {
                "source": "big_ben",
                "channel_mode": "ertm"
            }
        "#;
        let config =
            A2dpConfiguration::from_reader(&missing_domain[..]).expect("without domain config");
        assert_eq!(A2dpConfiguration::default().domain, config.domain);
        assert_eq!(AudioSourceType::BigBen, config.source);
        assert_eq!(true, config.enable_source);

        let missing_source = br#"
            {
                "domain": "Testing",
                "channel_mode": "ertm",
                "enable_sink": false
            }
        "#;
        let config =
            A2dpConfiguration::from_reader(&missing_source[..]).expect("without source config");
        assert_eq!(A2dpConfiguration::default().source, config.source);
        assert_eq!(bredr::ChannelMode::EnhancedRetransmission, config.channel_mode);
        assert_eq!(false, config.enable_sink);

        let missing_mode = br#"
            {
                "domain": "Testing",
                "source": "audio_out",
                "enable_source": false
            }
        "#;
        let config =
            A2dpConfiguration::from_reader(&missing_mode[..]).expect("without mode config");
        assert_eq!(A2dpConfiguration::default().channel_mode, config.channel_mode);
        assert_eq!("Testing", config.domain);
        assert_eq!(false, config.enable_source);

        let missing_all = b"{}";
        let config = A2dpConfiguration::from_reader(&missing_all[..]).expect("without everything");
        assert_eq!(A2dpConfiguration::default(), config);
    }
}
