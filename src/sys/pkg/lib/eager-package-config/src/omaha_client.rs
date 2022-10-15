// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use channel_config::ChannelConfigs;
use fuchsia_url::UnpinnedAbsolutePackageUrl;
use omaha_client::cup_ecdsa::PublicKeys;
use serde::{Deserialize, Serialize};
use std::io;

const EAGER_PACKAGE_CONFIG_PATH: &str = "/config/data/eager_package_config.json";

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct OmahaServer {
    pub service_url: String,
    pub public_keys: PublicKeys,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct EagerPackageConfig {
    pub url: UnpinnedAbsolutePackageUrl,
    pub flavor: Option<String>,
    pub channel_config: ChannelConfigs,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct EagerPackageConfigs {
    pub server: OmahaServer,
    pub packages: Vec<EagerPackageConfig>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct EagerPackageConfigsJson {
    pub eager_package_configs: Vec<EagerPackageConfigs>,
}

impl EagerPackageConfigs {
    /// Load the eager package config from namespace.
    pub fn from_namespace() -> Result<Self, Error> {
        let file = std::fs::File::open(EAGER_PACKAGE_CONFIG_PATH)
            .context("opening eager package config file")?;
        Self::from_reader(io::BufReader::new(file))
    }

    fn from_reader(reader: impl io::Read) -> Result<Self, Error> {
        let eager_package_configs_json: EagerPackageConfigsJson =
            serde_json::from_reader(reader).context("parsing eager package config")?;
        // Omaha client only supports one Omaha server at a time; just take the
        // first server config in this file.
        if eager_package_configs_json.eager_package_configs.len() > 1 {
            tracing::error!(
                "Warning: this eager package config JSON file contained more \
                than one Omaha server config, but omaha-client only supports \
                one Omaha server."
            );
        }
        let eager_package_configs =
            eager_package_configs_json.eager_package_configs.into_iter().next().ok_or(
                anyhow::anyhow!(
                    "Eager package config JSON did not contain any server-and-package configs."
                ),
            )?;
        for package in &eager_package_configs.packages {
            package.channel_config.validate().context("validating eager package channel config")?;
        }

        Ok(eager_package_configs)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use channel_config::ChannelConfig;
    use omaha_client::cup_ecdsa::{
        test_support::{
            make_default_json_public_keys_for_test, make_default_public_key_for_test,
            make_default_public_key_id_for_test,
        },
        PublicKeyAndId,
    };
    use pretty_assertions::assert_eq;

    #[test]
    fn parse_eager_package_configs_json() {
        let json = serde_json::json!(
        {
            "eager_package_configs": [
                {
                    "server": {
                        "service_url": "https://example.com",
                        "public_keys": make_default_json_public_keys_for_test(),
                    },
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
                }
            ]
        });

        assert_eq!(
            EagerPackageConfigs::from_reader(json.to_string().as_bytes()).unwrap(),
            EagerPackageConfigs {
                server: OmahaServer {
                    service_url: "https://example.com".into(),
                    public_keys: PublicKeys {
                        latest: PublicKeyAndId {
                            id: make_default_public_key_id_for_test(),
                            key: make_default_public_key_for_test(),
                        },
                        historical: vec![],
                    }
                },
                packages: vec![
                    EagerPackageConfig {
                        url: UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://example.com/package")
                            .unwrap(),
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
                        url: UnpinnedAbsolutePackageUrl::parse(
                            "fuchsia-pkg://example.com/package2"
                        )
                        .unwrap(),
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
            "eager_package_configs": [
                {
                    "server": {},
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
                }
            ]
        }"#;
        assert_matches!(EagerPackageConfigs::from_reader(&json[..]), Err(_));
    }
}
