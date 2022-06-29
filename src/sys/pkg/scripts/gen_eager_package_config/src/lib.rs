// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
    omaha_client::cup_ecdsa::{PublicKeyAndId, PublicKeys},
    serde::Deserialize,
    std::collections::HashMap,
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

pub type PublicKeysByServiceUrl = HashMap<String, PublicKeys>;

#[derive(Deserialize)]
struct Realm {
    /// The Omaha App ID for this realm.
    app_id: String,
    /// The list of channels for this realm.
    channels: Vec<String>,
}

#[derive(Deserialize)]
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
    service_url: Option<String>,
}

fn make_public_keys(service_url: &str, key_config: &PublicKeysByServiceUrl) -> PublicKeys {
    let public_keys = &key_config
        .get(service_url)
        .expect(&format!("could not find service_url {:?} in key_config map", service_url));
    PublicKeys {
        latest: PublicKeyAndId { key: public_keys.latest.key.into(), id: public_keys.latest.id },
        historical: public_keys
            .historical
            .iter()
            .map(|h| PublicKeyAndId { key: h.key.into(), id: h.id })
            .collect(),
    }
}

pub fn generate_omaha_client_config(
    input_configs: &Vec<InputConfig>,
    key_config: &PublicKeysByServiceUrl,
) -> OmahaConfigsJson {
    let mut default_packages: Vec<OmahaConfig> = vec![];
    let mut packages_by_service_url = HashMap::<String, Vec<OmahaConfig>>::new();

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

        if let Some(service_url) = input_config.service_url.as_ref() {
            packages_by_service_url.entry(service_url.clone()).or_default().push(package);
        } else {
            default_packages.push(package);
        }
    }

    let mut eager_package_configs = vec![];
    if !default_packages.is_empty() {
        eager_package_configs.push(OmahaConfigs { server: None, packages: default_packages });
    }
    for (k, v) in packages_by_service_url {
        eager_package_configs.push(OmahaConfigs {
            server: Some(OmahaServer {
                service_url: k.clone(),
                public_keys: make_public_keys(&k, key_config),
            }),
            packages: v,
        });
    }

    OmahaConfigsJson { eager_package_configs }
}

pub fn generate_pkg_resolver_config(
    configs: &Vec<InputConfig>,
    key_config: &PublicKeysByServiceUrl,
) -> ResolverConfigs {
    ResolverConfigs {
        packages: configs
            .iter()
            .map(|i| ResolverConfig {
                url: i.url.clone(),
                executable: i.executable.unwrap_or(false),
                public_keys: make_public_keys(
                    i.service_url.as_ref().expect("config has no service_url."),
                    key_config,
                ),
            })
            .collect(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use omaha_client::cup_ecdsa::{test_support, PublicKeyId};

    fn make_key_config() -> PublicKeysByServiceUrl {
        let (_, public_key) = test_support::make_keys_for_test();
        let public_key_id: PublicKeyId = 42.try_into().unwrap();
        HashMap::from([(
            "https://example.com".to_string(),
            test_support::make_public_keys_for_test(public_key_id, public_key),
        )])
    }

    fn make_configs() -> Vec<InputConfig> {
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
                service_url: Some("https://example.com".to_string()),
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
                service_url: Some("https://example.com".to_string()),
            },
            InputConfig {
                url: "fuchsia-pkg://example.com/package_noservice_1".parse().unwrap(),
                default_channel: None,
                flavor: None,
                executable: None,
                realms: vec![Realm {
                    app_id: "3c4d5e6f".to_string(),
                    channels: vec!["stable".to_string()],
                }],
                service_url: None,
            },
            InputConfig {
                url: "fuchsia-pkg://example.com/package_noservice_2".parse().unwrap(),
                default_channel: None,
                flavor: None,
                executable: None,
                realms: vec![Realm {
                    app_id: "4c5d6e7f".to_string(),
                    channels: vec!["stable".to_string()],
                }],
                service_url: None,
            },
        ]
    }

    fn compare_ignoring_whitespace(a: &str, b: &str) {
        assert_eq!(
            a.chars().filter(|c| !c.is_whitespace()).collect::<String>(),
            b.chars().filter(|c| !c.is_whitespace()).collect::<String>(),
        );
    }

    #[test]
    fn test_generate_omaha_client_config() {
        let key_config = make_key_config();
        let configs = make_configs();
        let omaha_client_config = generate_omaha_client_config(&configs, &key_config);
        let expected = r#"{
            "eager_package_configs": [
              {
                "server": null,
                "packages": [
                  {
                    "url": "fuchsia-pkg://example.com/package_noservice_1",
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
                    }
                  },
                  {
                    "url": "fuchsia-pkg://example.com/package_noservice_2",
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
                    }
                  }
                ]
              },
              {
                "server": {
                  "service_url": "https://example.com",
                  "public_keys": {
                    "latest": {
                      "key": [ 48, 89, 48, 19, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 8, 42, 134, 72, 206, 61, 3, 1, 7, 3, 66, 0, 4, 28, 172, 255, 181, 95, 47, 44, 239, 216, 157, 137, 235, 55, 75, 38, 129, 21, 36, 82, 128, 45, 238, 160, 153, 22, 6, 129, 55, 216, 57, 207, 127, 196, 129, 164, 68, 146, 48, 77, 126, 246, 106, 193, 23, 190, 254, 131, 168, 208, 143, 21, 95, 43, 82, 249, 246, 24, 221, 68, 112, 41, 4, 142, 15 ],
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
                    }
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
                    }
                  }
                ]
              }
            ]
        }"#;
        compare_ignoring_whitespace(
            &serde_json::to_string_pretty(&omaha_client_config).unwrap(),
            &expected,
        );
    }

    #[test]
    #[should_panic(expected = "Default channel must appear in some realm's channel.")]
    fn test_generate_omaha_client_config_wrong_default_channel() {
        let key_config = make_key_config();
        let configs = vec![InputConfig {
            url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
            default_channel: Some("wrong".to_string()),
            flavor: None,
            executable: None,
            realms: vec![Realm {
                app_id: "1a2b3c4d".to_string(),
                channels: vec!["stable".to_string(), "beta".to_string(), "alpha".to_string()],
            }],
            service_url: None,
        }];
        let _omaha_client_config = generate_omaha_client_config(&configs, &key_config);
    }

    #[test]
    fn test_generate_omaha_client_config_no_default() {
        // If there is no default package, we should not create an empty config.
        // Here, a default package is any package without a service URL.
        let key_config = make_key_config();
        let configs = vec![InputConfig {
            url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
            default_channel: Some("stable".to_string()),
            flavor: Some("debug".to_string()),
            executable: Some(true),
            realms: vec![
                Realm {
                    app_id: "1a2b3c4d".to_string(),
                    channels: vec!["stable".to_string(), "beta".to_string(), "alpha".to_string()],
                },
                Realm { app_id: "2b3c4d5e".to_string(), channels: vec!["test".to_string()] },
            ],
            service_url: Some("https://example.com".to_string()),
        }];
        let omaha_client_config = generate_omaha_client_config(&configs, &key_config);
        let expected = r#"{
            "eager_package_configs": [
                {
                    "server": {
                        "service_url": "https://example.com",
                        "public_keys": {
                            "latest": {
                                "key": [48,89,48,19,6,7,42,134,72,206,61,2,1,6,8,42,134,72,206,61,3,1,7,3,66,0,4,28,172,255,181,95,47,44,239,216,157,137,235,55,75,38,129,21,36,82,128,45,238,160,153,22,6,129,55,216,57,207,127,196,129,164,68,146,48,77,126,246,106,193,23,190,254,131,168,208,143,21,95,43,82,249,246,24,221,68,112,41,4,142,15],
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
                            }
                        }
                    ]
            }
        ]}"#;
        compare_ignoring_whitespace(
            &serde_json::to_string_pretty(&omaha_client_config).unwrap(),
            &expected,
        );
    }

    #[test]
    fn test_generate_pkg_resolver_config() {
        let key_config = make_key_config();
        let configs = vec![InputConfig {
            url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
            default_channel: Some("stable".to_string()),
            flavor: None,
            executable: None,
            realms: vec![Realm {
                app_id: "1a2b3c4d".to_string(),
                channels: vec!["stable".to_string(), "beta".to_string(), "alpha".to_string()],
            }],
            service_url: Some("https://example.com".to_string()),
        }];
        let pkg_resolver_config = generate_pkg_resolver_config(&configs, &key_config);
        let expected = r#"{
            "packages":[
                {
                    "url": "fuchsia-pkg://example.com/package_service_1",
                    "executable": false,
                    "public_keys": {
                        "latest": {
                            "key": [48,89,48,19,6,7,42,134,72,206,61,2,1,6,8,42,134,72,206,61,3,1,7,3,66,0,4,28,172,255,181,95,47,44,239,216,157,137,235,55,75,38,129,21,36,82,128,45,238,160,153,22,6,129,55,216,57,207,127,196,129,164,68,146,48,77,126,246,106,193,23,190,254,131,168,208,143,21,95,43,82,249,246,24,221,68,112,41,4,142,15],
                            "id": 42
                        },
                        "historical": []
                    }
                }
            ]
        }"#;
        compare_ignoring_whitespace(
            &serde_json::to_string_pretty(&pkg_resolver_config).unwrap(),
            &expected,
        );
    }
}
