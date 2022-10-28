// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    argh::FromArgs,
    channel_config::{ChannelConfig, ChannelConfigs},
    eager_package_config::omaha_client::EagerPackageConfig as OmahaConfig,
    eager_package_config::omaha_client::EagerPackageConfigs as OmahaConfigs,
    eager_package_config::omaha_client::EagerPackageConfigsJson as OmahaConfigsJson,
    eager_package_config::omaha_client::OmahaServer,
    eager_package_config::pkg_resolver::EagerPackageConfig as ResolverConfig,
    eager_package_config::pkg_resolver::EagerPackageConfigs as ResolverConfigs,
    fuchsia_url::UnpinnedAbsolutePackageUrl,
    omaha_client::cup_ecdsa::PublicKeys,
    serde::{Deserialize, Serialize},
    std::collections::{BTreeMap, HashSet},
    version::Version,
};

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(description = "gen_eager_package_config arguments")]
pub struct Args {
    #[argh(
        option,
        description = "path to the generated eager package config file for omaha-client"
    )]
    pub out_omaha_client_config: String,
    #[argh(
        option,
        description = "path to the generated eager package config file for pkg-resolver"
    )]
    pub out_pkg_resolver_config: String,
    #[argh(option, description = "JSON key config file, with map from service URL to public keys")]
    pub key_config_file: String,
    #[argh(positional, description = "JSON config files, one for each eager package")]
    pub eager_package_config_files: Vec<String>,
}

// Prefer a BTreeMap so that the deserialized map has a consistent ordering when
// printed as a list of key, value pairs.
pub type PublicKeysByServiceUrl = BTreeMap<String, PublicKeys>;

#[derive(Serialize, Deserialize)]
struct Realm {
    /// The Omaha App ID for this realm.
    app_id: String,
    /// The list of channels for this realm.
    channels: Vec<String>,
}

#[derive(Serialize, Deserialize)]
pub struct InputConfig {
    /// The URL of the package.
    url: UnpinnedAbsolutePackageUrl,
    /// The flavor of the package.
    flavor: Option<String>,
    /// The executability of the package.
    executable: Option<bool>,
    /// If set, this channel will be the default channel. The channel must
    /// appear in channels in at least one realm.
    default_channel: Option<String>,
    /// List of realms.
    realms: Vec<Realm>,
    /// The URL of the Omaha server.
    service_url: String,
    /// The minimum required version of the package.
    minimum_required_version: Version,
    cache_fallback: Option<bool>,
}

pub fn generate_omaha_client_config(
    input_configs: &Vec<InputConfig>,
    key_configs: &PublicKeysByServiceUrl,
) -> OmahaConfigsJson {
    let mut packages_by_service_url = BTreeMap::<String, Vec<OmahaConfig>>::new();

    for input_config in input_configs {
        let mut channel_configs = ChannelConfigs {
            default_channel: input_config.default_channel.clone(),
            known_channels: vec![],
        };

        if let Some(dc) = &input_config.default_channel {
            if !(&input_config.realms.iter().any(|realm| realm.channels.contains(dc))) {
                panic!("Default channel must appear in some realm's channel.");
            }
        }

        for realm in &input_config.realms {
            for channel in &realm.channels {
                channel_configs.known_channels.push(ChannelConfig {
                    name: channel.clone(),
                    repo: channel.clone(),
                    appid: Some(realm.app_id.clone()),
                    check_interval_secs: None,
                });
            }
        }

        let package = OmahaConfig {
            url: input_config.url.clone(),
            flavor: input_config.flavor.clone(),
            channel_config: channel_configs,
        };

        packages_by_service_url.entry(input_config.service_url.clone()).or_default().push(package);
    }

    let json = OmahaConfigsJson {
        eager_package_configs: key_configs
            .iter()
            .map(|(service_url, key_config)| OmahaConfigs {
                server: OmahaServer {
                    service_url: service_url.clone(),
                    public_keys: key_config.clone(),
                },
                packages: packages_by_service_url.remove(service_url).unwrap_or_default(),
            })
            .collect(),
    };
    assert!(packages_by_service_url.is_empty(),
      "There were service_url keys present in the input configs which did not have a corresponding keyconfig: {:?}",
      packages_by_service_url.keys());
    json
}

pub fn generate_pkg_resolver_config(
    configs: &[InputConfig],
    key_config: &PublicKeysByServiceUrl,
) -> ResolverConfigs {
    let packages: Vec<_> = configs
        .iter()
        .map(|i| ResolverConfig {
            url: i.url.clone(),
            executable: i.executable.unwrap_or(false),
            public_keys: key_config
                .get(&i.service_url)
                .unwrap_or_else(|| {
                    panic!("could not find service_url {:?} in key_config map", i.service_url)
                })
                .clone(),
            minimum_required_version: i.minimum_required_version,
            cache_fallback: i.cache_fallback.unwrap_or(true),
        })
        .collect();
    if packages.iter().map(|config| config.url.path()).collect::<HashSet<_>>().len()
        < packages.len()
    {
        panic!("Eager package URL must have unique path");
    }
    ResolverConfigs { packages }
}

pub mod test_support {
    use super::*;
    use omaha_client::cup_ecdsa::{test_support, PublicKeyId};

    pub fn make_key_config_for_test() -> PublicKeysByServiceUrl {
        let (_, public_key) = test_support::make_keys_for_test();
        let public_key_id_a: PublicKeyId = 42.try_into().unwrap();
        let public_key_id_b: PublicKeyId = 43.try_into().unwrap();
        BTreeMap::from([
            (
                "https://example.com".to_string(),
                test_support::make_public_keys_for_test(public_key_id_a, public_key),
            ),
            (
                "https://other_example.com".to_string(),
                test_support::make_public_keys_for_test(public_key_id_b, public_key),
            ),
        ])
    }

    pub fn make_configs_for_test() -> Vec<InputConfig> {
        vec![
            InputConfig {
                url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
                default_channel: Some("stable".to_string()),
                flavor: Some("debug".to_string()),
                executable: Some(true),
                realms: vec![
                    Realm {
                        app_id: "1a2b3c4d".to_string(),
                        channels: vec![
                            "stable".to_string(),
                            "beta".to_string(),
                            "alpha".to_string(),
                        ],
                    },
                    Realm { app_id: "2b3c4d5e".to_string(), channels: vec!["test".to_string()] },
                ],
                service_url: "https://example.com".to_string(),
                minimum_required_version: [1, 2, 3, 4].into(),
                cache_fallback: Some(true),
            },
            InputConfig {
                url: "fuchsia-pkg://example.com/package_service_2".parse().unwrap(),
                default_channel: None,
                flavor: None,
                executable: None,
                realms: vec![Realm {
                    app_id: "5c6d7e8f".to_string(),
                    channels: vec!["stable".to_string()],
                }],
                service_url: "https://example.com".to_string(),
                minimum_required_version: [1, 2, 3, 4].into(),
                cache_fallback: None,
            },
            InputConfig {
                url: "fuchsia-pkg://example.com/package_otherservice_1".parse().unwrap(),
                default_channel: None,
                flavor: None,
                executable: None,
                realms: vec![Realm {
                    app_id: "3c4d5e6f".to_string(),
                    channels: vec!["stable".to_string()],
                }],
                service_url: "https://other_example.com".to_string(),
                minimum_required_version: [1, 2, 3, 4].into(),
                cache_fallback: None,
            },
            InputConfig {
                url: "fuchsia-pkg://example.com/package_otherservice_2".parse().unwrap(),
                default_channel: None,
                flavor: None,
                executable: None,
                realms: vec![Realm {
                    app_id: "4c5d6e7f".to_string(),
                    channels: vec!["stable".to_string()],
                }],
                service_url: "https://other_example.com".to_string(),
                minimum_required_version: [1, 2, 3, 4].into(),
                cache_fallback: None,
            },
        ]
    }

    pub fn compare_ignoring_whitespace(a: &str, b: &str) {
        assert_eq!(
            a.chars().filter(|c| !c.is_whitespace()).collect::<String>(),
            b.chars().filter(|c| !c.is_whitespace()).collect::<String>(),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support;

    #[test]
    fn test_generate_omaha_client_config_empty() {
        let key_config = test_support::make_key_config_for_test();
        let empty_configs = vec![];
        let omaha_client_config = generate_omaha_client_config(&empty_configs, &key_config);
        let expected = r#"{
            "eager_package_configs": [
              {
                "server": {
                  "service_url": "https://example.com",
                  "public_keys": {
                    "latest": {
                      "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----\n",
                      "id": 42
                    },
                    "historical": []
                  }
                },
                "packages": []
              },
              {
                "server": {
                  "service_url": "https://other_example.com",
                  "public_keys": {
                    "latest": {
                      "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----\n",
                      "id": 43
                    },
                    "historical": []
                  }
                },
                "packages": []
              }
            ]
        }"#;
        assert_eq!(
            omaha_client_config,
            serde_json::from_str::<OmahaConfigsJson>(expected).unwrap()
        );
    }

    #[test]
    fn test_generate_omaha_client_config() {
        let key_config = test_support::make_key_config_for_test();
        let configs = test_support::make_configs_for_test();
        let omaha_client_config = generate_omaha_client_config(&configs, &key_config);
        let expected = r#"{
            "eager_package_configs": [
              {
                "server": {
                  "service_url": "https://example.com",
                  "public_keys": {
                    "latest": {
                      "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----\n",
                      "id": 42
                    },
                    "historical": []
                  }
                },
                "packages": [
                  {
                    "url": "fuchsia-pkg://example.com/package_service_1",
                    "flavor": "debug",
                    "channel_config": {
                      "default_channel": "stable",
                      "channels": [
                        {
                          "name": "stable",
                          "repo": "stable",
                          "appid": "1a2b3c4d",
                          "check_interval_secs": null
                        },
                        {
                          "name": "beta",
                          "repo": "beta",
                          "appid": "1a2b3c4d",
                          "check_interval_secs": null
                        },
                        {
                          "name": "alpha",
                          "repo": "alpha",
                          "appid": "1a2b3c4d",
                          "check_interval_secs": null
                        },
                        {
                          "name": "test",
                          "repo": "test",
                          "appid": "2b3c4d5e",
                          "check_interval_secs": null
                        }
                      ]
                    },
                    "minimum_required_version": "1.2.3.4"
                  },
                  {
                    "url": "fuchsia-pkg://example.com/package_service_2",
                    "flavor": null,
                    "channel_config": {
                      "default_channel": null,
                      "channels": [
                        {
                          "name": "stable",
                          "repo": "stable",
                          "appid": "5c6d7e8f",
                          "check_interval_secs": null
                        }
                      ]
                    },
                    "minimum_required_version": "1.2.3.4"
                  }
                ]
              },
              {
                "server": {
                  "service_url": "https://other_example.com",
                  "public_keys": {
                    "latest": {
                      "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----\n",
                      "id": 43
                    },
                    "historical": []
                  }
                },
                "packages": [
                  {
                    "url": "fuchsia-pkg://example.com/package_otherservice_1",
                    "flavor": null,
                    "channel_config": {
                      "default_channel": null,
                      "channels": [
                        {
                          "name": "stable",
                          "repo": "stable",
                          "appid": "3c4d5e6f",
                          "check_interval_secs": null
                        }
                      ]
                    },
                    "minimum_required_version": "1.2.3.4"
                  },
                  {
                    "url": "fuchsia-pkg://example.com/package_otherservice_2",
                    "flavor": null,
                    "channel_config": {
                      "default_channel": null,
                      "channels": [
                        {
                          "name": "stable",
                          "repo": "stable",
                          "appid": "4c5d6e7f",
                          "check_interval_secs": null
                        }
                      ]
                    },
                    "minimum_required_version": "1.2.3.4"
                  }
                ]
              }
            ]
        }"#;
        assert_eq!(
            omaha_client_config,
            serde_json::from_str::<OmahaConfigsJson>(expected).unwrap()
        );
    }

    #[test]
    #[should_panic(expected = "Default channel must appear in some realm's channel.")]
    fn test_generate_omaha_client_config_wrong_default_channel() {
        let key_config = test_support::make_key_config_for_test();
        let configs = vec![InputConfig {
            url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
            default_channel: Some("wrong".to_string()),
            flavor: None,
            executable: None,
            realms: vec![Realm {
                app_id: "1a2b3c4d".to_string(),
                channels: vec!["stable".to_string(), "beta".to_string(), "alpha".to_string()],
            }],
            service_url: "https://example.com".to_string(),
            minimum_required_version: [1, 2, 3, 4].into(),
            cache_fallback: None,
        }];
        let _omaha_client_config = generate_omaha_client_config(&configs, &key_config);
    }

    #[test]
    fn test_generate_pkg_resolver_config() {
        let key_config = test_support::make_key_config_for_test();
        let configs = vec![InputConfig {
            url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
            default_channel: Some("stable".to_string()),
            flavor: None,
            executable: None,
            realms: vec![Realm {
                app_id: "1a2b3c4d".to_string(),
                channels: vec!["stable".to_string(), "beta".to_string(), "alpha".to_string()],
            }],
            service_url: "https://example.com".to_string(),
            minimum_required_version: [1, 2, 3, 4].into(),
            cache_fallback: Some(false),
        }];
        let pkg_resolver_config = generate_pkg_resolver_config(&configs, &key_config);
        let expected = r#"{
            "packages":[
                {
                    "url": "fuchsia-pkg://example.com/package_service_1",
                    "public_keys": {
                        "latest": {
                            "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----\n",
                            "id": 42
                        },
                        "historical": []
                    },
                    "minimum_required_version": "1.2.3.4",
                    "cache_fallback": false
                }
            ]
        }"#;
        test_support::compare_ignoring_whitespace(
            &serde_json::to_string_pretty(&pkg_resolver_config).unwrap(),
            expected,
        );
    }

    #[test]
    #[should_panic(expected = "Eager package URL must have unique path")]
    fn test_generate_pkg_resolver_config_duplicate_path() {
        let key_config = test_support::make_key_config_for_test();
        let configs = vec![
            InputConfig {
                url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
                default_channel: Some("stable".to_string()),
                flavor: None,
                executable: None,
                realms: vec![Realm {
                    app_id: "1a2b3c4d".to_string(),
                    channels: vec!["stable".to_string(), "beta".to_string(), "alpha".to_string()],
                }],
                service_url: "https://example.com".to_string(),
                minimum_required_version: [1, 2, 3, 4].into(),
                cache_fallback: None,
            },
            InputConfig {
                url: "fuchsia-pkg://another-example.com/package_service_1".parse().unwrap(),
                default_channel: None,
                flavor: None,
                executable: None,
                realms: vec![],
                service_url: "https://example.com".to_string(),
                minimum_required_version: [1, 2, 3, 4].into(),
                cache_fallback: None,
            },
        ];
        let _ = generate_pkg_resolver_config(&configs, &key_config);
    }
}
