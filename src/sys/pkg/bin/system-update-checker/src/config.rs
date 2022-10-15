// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fuchsia_url::AbsolutePackageUrl,
    fuchsia_zircon::Duration,
    serde::Deserialize,
    std::{cmp, fs::File, io::Read, num::NonZeroU64},
    thiserror::Error,
    tracing::{error, info},
};

/// Static service configuration options.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct Config {
    poll_frequency: Option<Duration>,
    update_package_url: Option<AbsolutePackageUrl>,
}

impl Config {
    pub fn poll_frequency(&self) -> Option<Duration> {
        self.poll_frequency
    }

    pub fn update_package_url(&self) -> Option<&AbsolutePackageUrl> {
        self.update_package_url.as_ref()
    }

    pub fn load_from_config_data_or_default() -> Config {
        let f = match File::open("/config/data/ota_config.json") {
            Ok(f) => f,
            Err(e) => {
                info!("no config found, using defaults: {:#}", anyhow!(e));
                return Config::default();
            }
        };

        Self::load(f).unwrap_or_else(|e| {
            error!("unable to load config, using defaults: {:#}", anyhow!(e));
            Config::default()
        })
    }

    fn load(r: impl Read) -> Result<Config, ConfigLoadError> {
        #[derive(Debug, Deserialize)]
        #[serde(deny_unknown_fields)]
        struct ParseConfig {
            poll_frequency_minutes: Option<NonZeroU64>,
            update_package_url: Option<AbsolutePackageUrl>,
        }

        let config = serde_json::from_reader::<_, ParseConfig>(r)?;

        Ok(Config {
            poll_frequency: config.poll_frequency_minutes.map(|freq| {
                // zx::Duration will wrap on overflow when converting to nanoseconds. Ensure a
                // config file cannot specify a negative duration by clamping to the maximum number
                // of minutes that can be represented as nanoseconds in an i64.
                let max_minutes_duration =
                    Duration::from_nanos(std::i64::MAX).into_minutes() as u64;
                Duration::from_minutes(cmp::min(freq.get(), max_minutes_duration) as i64)
            }),
            update_package_url: config.update_package_url,
        })
    }
}

#[derive(Debug, Error)]
enum ConfigLoadError {
    #[error("parse error")]
    Parse(#[from] serde_json::Error),
}

#[cfg(test)]
#[derive(Debug)]
pub struct ConfigBuilder(Config);

#[cfg(test)]
impl ConfigBuilder {
    pub fn new() -> Self {
        Self(Config::default())
    }

    pub fn poll_frequency(mut self, duration: impl Into<Duration>) -> Self {
        self.0.poll_frequency = Some(duration.into());
        self
    }

    pub fn build(self) -> Config {
        self.0
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, serde_json::json};

    fn verify_load(input: serde_json::Value, expected: Config) {
        let input = input.to_string();

        assert_eq!(Config::load(input.as_bytes()).unwrap(), expected);
    }

    #[test]
    fn test_load() {
        verify_load(
            json!({
                "update_package_url": "fuchsia-pkg://fuchsia.test/abc",
                "poll_frequency_minutes": 123,
            }),
            Config {
                poll_frequency: Some(Duration::from_minutes(123)),
                update_package_url: Some(
                    AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.test/abc").unwrap(),
                ),
            },
        );
    }

    #[test]
    fn test_missing_fields_are_defaults() {
        verify_load(
            json!({
                "update_package_url": "fuchsia-pkg://fuchsia.test/the-update",
            }),
            Config {
                poll_frequency: None,
                update_package_url: Some(
                    AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.test/the-update").unwrap(),
                ),
            },
        );
        verify_load(
            json!({
                "poll_frequency_minutes": 1,
            }),
            Config { poll_frequency: Some(Duration::from_minutes(1)), update_package_url: None },
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
    fn test_load_rejects_invalid() {
        assert_matches!(
            Config::load("not json".as_bytes()),
            Err(ConfigLoadError::Parse(ref err)) if err.is_syntax());
    }

    #[test]
    fn test_load_rejects_zero_poll_frequency() {
        let input = json!({
            "poll_frequency_minutes": 0,
        })
        .to_string();

        assert_matches!(
            Config::load(input.as_bytes()),
            Err(ConfigLoadError::Parse(ref err)) if err.is_data());
    }

    #[test]
    fn test_load_clamps_large_poll_frequency() {
        let max_duration = Duration::from_nanos(std::i64::MAX);
        let max_duration_minutes = max_duration.into_minutes();

        let verify_clamp = |minutes| {
            verify_load(
                json!({
                    "poll_frequency_minutes": minutes,
                }),
                Config {
                    poll_frequency: Some(Duration::from_minutes(max_duration_minutes)),
                    update_package_url: None,
                },
            );
        };

        verify_clamp(max_duration_minutes as u64);
        verify_clamp(max_duration_minutes as u64 + 1);
        verify_clamp(std::i64::MAX as u64);
        verify_clamp(std::u64::MAX);
    }
}
