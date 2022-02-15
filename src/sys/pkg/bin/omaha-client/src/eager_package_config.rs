// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::channel::ChannelConfigs;
use anyhow::{Context as _, Error};
use fuchsia_url::pkg_url::PkgUrl;
use serde::Deserialize;
use std::io;

const EAGER_PACKAGE_CONFIG_PATH: &str = "/config/data/eager_package_config.json";

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub struct EagerPackageConfig {
    pub url: PkgUrl,
    pub flavor: Option<String>,
    pub channel_config: ChannelConfigs,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub struct EagerPackageConfigs {
    pub packages: Vec<EagerPackageConfig>,
}

impl EagerPackageConfigs {
    /// Load the eager package config from namespace.
    pub fn from_namespace() -> Result<Self, Error> {
        let file = std::fs::File::open(EAGER_PACKAGE_CONFIG_PATH)
            .context("opening eager package config file")?;
        Self::from_reader(io::BufReader::new(file))
    }

    fn from_reader(reader: impl io::Read) -> Result<Self, Error> {
        let configs: Self =
            serde_json::from_reader(reader).context("parsing eager package config")?;
        for package in &configs.packages {
            package.channel_config.validate().context("validating eager package channel config")?;
        }

        Ok(configs)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::channel::ChannelConfig;
    use assert_matches::assert_matches;
    use pretty_assertions::assert_eq;

    #[test]
    fn parse_eager_package_configs_json() {
        let json = br#"
        {
            "packages":
            [
                {
                    "url": "fuchsia-pkg://example.com/package",
                    "flavor": "debug",
                    "channel_config":
                        {
                            "channels":
                                [
                                    {
                                        "name": "stable",
                                        "repo": "stable",
                                        "appid": "1a2b3c4d"
                                    },
                                    {
                                        "name": "beta",
                                        "repo": "beta",
                                        "appid": "1a2b3c4d"
                                    },
                                    {
                                        "name": "alpha",
                                        "repo": "alpha",
                                        "appid": "1a2b3c4d"
                                    },
                                    {
                                        "name": "test",
                                        "repo": "test",
                                        "appid": "2b3c4d5e"
                                    }
                                ],
                            "default_channel": "stable"
                        }
                },
                {
                    "url": "fuchsia-pkg://example.com/package2",
                    "channel_config":
                        {
                            "channels":
                                [
                                    {
                                        "name": "stable",
                                        "repo": "stable",
                                        "appid": "3c4d5e6f"
                                    }
                                ]
                        }
                }
            ]
        }"#;
        assert_eq!(
            EagerPackageConfigs::from_reader(&json[..]).unwrap(),
            EagerPackageConfigs {
                packages: vec![
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package").unwrap(),
                        flavor: Some("debug".into()),
                        channel_config: ChannelConfigs {
                            default_channel: Some("stable".into()),
                            known_channels: vec![
                                ChannelConfig {
                                    name: "stable".into(),
                                    repo: "stable".into(),
                                    appid: Some("1a2b3c4d".into()),
                                    check_interval_secs: None,
                                },
                                ChannelConfig {
                                    name: "beta".into(),
                                    repo: "beta".into(),
                                    appid: Some("1a2b3c4d".into()),
                                    check_interval_secs: None,
                                },
                                ChannelConfig {
                                    name: "alpha".into(),
                                    repo: "alpha".into(),
                                    appid: Some("1a2b3c4d".into()),
                                    check_interval_secs: None,
                                },
                                ChannelConfig {
                                    name: "test".into(),
                                    repo: "test".into(),
                                    appid: Some("2b3c4d5e".into()),
                                    check_interval_secs: None,
                                },
                            ]
                        },
                    },
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap(),
                        flavor: None,
                        channel_config: ChannelConfigs {
                            default_channel: None,
                            known_channels: vec![ChannelConfig {
                                name: "stable".into(),
                                repo: "stable".into(),
                                appid: Some("3c4d5e6f".into()),
                                check_interval_secs: None,
                            },]
                        },
                    },
                ]
            }
        );
    }

    #[test]
    fn parse_eager_package_configs_json_reject_invalid() {
        let json = br#"
        {
            "packages":
            [
                {
                    "url": "fuchsia-pkg://example.com/package",
                    "channel_config":
                        {
                            "channels":
                                [
                                    {
                                        "name": "stable",
                                        "repo": "stable",
                                    }
                                ],
                            "default_channel": "invalid"
                        }
                }
            ]
        }"#;
        assert_matches!(EagerPackageConfigs::from_reader(&json[..]), Err(_));
    }
}
