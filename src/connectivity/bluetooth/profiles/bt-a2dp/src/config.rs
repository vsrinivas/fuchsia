// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use a2dp_profile_config::Config;
use anyhow::{format_err, Error};
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_zircon as zx;
use std::collections::HashSet;
use thiserror::Error;

use crate::sources::AudioSourceType;

pub(crate) const DEFAULT_DOMAIN: &str = "Bluetooth";
pub(crate) const DEFAULT_INITIATOR_DELAY: zx::Duration = zx::Duration::from_millis(500);

/// The MAX receive SDU size to ask the Profile Server when connecting or accepting a L2CAP
/// connection.
/// This is a compromise between packets containing more audio data and limiting retransmission
/// cost in the case of a flaky link.  The current default fits within a single 3-DH5 packet after
/// ACL and L2CAP headers are added.
pub(crate) const MAX_RX_SDU_SIZE: u16 = 1014;

/// Parses the ChannelMode
///
/// Returns an Error if the provided argument is an invalid string.
fn channel_mode_from_str(channel_mode: String) -> Result<bredr::ChannelMode, Error> {
    match channel_mode.as_str() {
        "basic" => Ok(bredr::ChannelMode::Basic),
        "ertm" => Ok(bredr::ChannelMode::EnhancedRetransmission),
        s => return Err(format_err!("invalid channel mode: {}", s)),
    }
}

/// Configuration parameters for A2DP.
/// Typically loaded from a config file provided during build.
/// See [`A2dpConfiguration::load_default`]
#[derive(Clone, Debug)]
#[cfg_attr(test, derive(PartialEq))]
pub struct A2dpConfiguration {
    /// The media session domain which is reported to the Fuchsia media system.
    pub domain: String,
    /// The source for audio sent to sinks connected to this profile, if None, source is disabled.
    pub source: Option<AudioSourceType>,
    /// Mode used for A2DP signaling channel establishment.
    pub channel_mode: bredr::ChannelMode,
    /// Enable sink streams. defaults to true.
    pub enable_sink: bool,
    /// Enable AVRCP-Target. defaults to true.
    pub enable_avrcp_target: bool,
    /// Enable using the AAC codec.  Has no effect if AAC is not available. defaults to true.
    pub enable_aac: bool,
    /// Duration for A2DP to wait before assuming role of the initiator.
    /// If a signaling channel has not been established by this time, A2DP will
    /// create the signaling channel, configure, open and start the stream. Defaults
    /// to 500 milliseconds. Set to 0 to disable initiation.
    pub initiator_delay: zx::Duration,
}

impl Default for A2dpConfiguration {
    fn default() -> Self {
        A2dpConfiguration {
            domain: DEFAULT_DOMAIN.into(),
            source: Some(AudioSourceType::AudioOut),
            channel_mode: bredr::ChannelMode::Basic,
            enable_sink: true,
            enable_avrcp_target: true,
            enable_aac: true,
            initiator_delay: DEFAULT_INITIATOR_DELAY,
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
    /// Returns Error if there are syntax or configuration errors.
    pub fn load_default() -> Result<Self, Error> {
        let config = Config::take_from_startup_handle();
        Self::load_default_with_config(config)
    }

    /// Loads configuration using the default method
    /// The defaults are used, `str_config` overriding them.
    /// Returns Error if there are syntax or configuration errors.
    fn load_default_with_config(str_config: Config) -> Result<Self, Error> {
        let configured = Self::default();
        let merged = configured.merge(str_config)?;
        let problems = merged.errors();
        if !problems.is_empty() {
            return Err(format_err!("Configuration unsupported: {:?}", problems));
        }
        Ok(merged)
    }

    pub fn merge(self, str_config: Config) -> Result<Self, Error> {
        let Config {
            domain,
            source_type,
            channel_mode,
            enable_sink,
            enable_avrcp_target,
            enable_aac,
            initiator_delay,
        } = str_config;
        let source = if source_type == "none" { None } else { Some(source_type.parse()?) };
        let channel_mode = channel_mode_from_str(channel_mode)?;
        let initiator_delay = zx::Duration::from_millis(initiator_delay.into());
        Ok(Self {
            domain,
            source,
            channel_mode,
            enable_sink,
            enable_avrcp_target,
            enable_aac,
            initiator_delay,
            ..self
        })
    }

    /// Returns the set of preferred channel parameters for outgoing and incoming L2CAP connections.
    pub fn channel_parameters(&self) -> bredr::ChannelParameters {
        bredr::ChannelParameters {
            channel_mode: Some(self.channel_mode),
            max_rx_sdu_size: Some(MAX_RX_SDU_SIZE),
            ..bredr::ChannelParameters::EMPTY
        }
    }

    /// Returns a set of configuration problems with the current configuration.
    pub fn errors(&self) -> HashSet<ConfigurationError> {
        let mut e = HashSet::new();
        if !(self.enable_sink || self.source.is_some()) {
            let _ = e.insert(ConfigurationError::NoProfilesEnabled);
        }
        e
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
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
    fn test_source_from_str() {
        let config = Config {
            domain: "Bluetooth".to_string(),
            source_type: "none".to_string(),
            channel_mode: "basic".to_string(),
            enable_sink: true,
            enable_avrcp_target: true,
            enable_aac: true,
            initiator_delay: 500,
        };

        let result = A2dpConfiguration::default().merge(config).unwrap();
        assert_eq!(None, result.source);

        let config = Config {
            domain: "Bluetooth".to_string(),
            source_type: "audio_out".to_string(),
            channel_mode: "basic".to_string(),
            enable_sink: true,
            enable_avrcp_target: true,
            enable_aac: true,
            initiator_delay: 500,
        };

        let result = A2dpConfiguration::default().merge(config).unwrap();
        assert_eq!(Some(AudioSourceType::AudioOut), result.source);
    }

    #[test]
    fn unsupported_configs() {
        let config = A2dpConfiguration { source: None, enable_sink: false, ..Default::default() };
        let problems = config.errors();
        assert!(problems.contains(&ConfigurationError::NoProfilesEnabled));
    }

    #[test]
    fn generates_correct_parameters() {
        let mut config = A2dpConfiguration::default();
        assert_eq!(Some(MAX_RX_SDU_SIZE), config.channel_parameters().max_rx_sdu_size);
        assert_eq!(Some(bredr::ChannelMode::Basic), config.channel_parameters().channel_mode);
        config.channel_mode = bredr::ChannelMode::EnhancedRetransmission;
        assert_eq!(
            Some(bredr::ChannelMode::EnhancedRetransmission),
            config.channel_parameters().channel_mode
        );
    }
}
