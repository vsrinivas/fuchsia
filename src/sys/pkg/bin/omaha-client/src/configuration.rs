// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::app_set::{AppIdSource, AppMetadata, EagerPackage, FuchsiaAppSet};
use anyhow::{anyhow, Error};
use channel_config::{ChannelConfig, ChannelConfigs};
use eager_package_config::omaha_client::{EagerPackageConfig, EagerPackageConfigs};
use fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsProxy};
use fidl_fuchsia_pkg::{self as fpkg, CupMarker, CupProxy, GetInfoError};
use omaha_client::{
    common::App,
    configuration::{Config, Updater},
    protocol::{request::OS, Cohort},
};
use std::collections::HashMap;
use std::fs;
use std::io;
use std::iter::FromIterator;
use tracing::{error, info, warn};
use version::Version;

// TODO: This is not 0.0.0.0 because that would cause state machine to not start. We should find a
// better way to achieve that when build version is invalid.
const MINIMUM_VALID_VERSION: [u32; 4] = [0, 0, 0, 1];

/// This struct is the overall "configuration" of the omaha client.  Minus the PolicyConfig.  That
/// should probably be included in here as well, eventually.
pub struct ClientConfiguration {
    pub platform_config: omaha_client::configuration::Config,
    pub app_set: FuchsiaAppSet,
    pub channel_data: ChannelData,
}

/// This wraps up all the channel-related configuration (name, appid, config, and any other params
/// that they might have).  Mostly as a grouping for returning from fns in a cleaner way than a
/// tuple provides.
pub struct ChannelData {
    pub source: ChannelSource,
    pub name: Option<String>,
    pub config: Option<ChannelConfig>,
}

/// The source of the channel configuration.
#[derive(Debug, Eq, PartialEq)]
pub enum ChannelSource {
    MinFS,
    Default,
    VbMeta,
}

fn get_appid(
    vbmeta_appid: Option<String>,
    channel_config: &Option<ChannelConfig>,
) -> (String, AppIdSource) {
    if let Some(appid) = vbmeta_appid {
        return (appid, AppIdSource::VbMetadata);
    }

    // If no appid in vbmeta, look up the appid of the channel from the channel config.
    if let Some(config) = channel_config {
        if let Some(appid) = &config.appid {
            return (appid.clone(), AppIdSource::ChannelConfig);
        }
    }

    // If no appid in the channel configs, then attempt to read from config data.
    match fs::read_to_string("/config/data/omaha_app_id") {
        Ok(id) => (id, AppIdSource::ConfigData),
        Err(e) => {
            error!("Unable to read omaha app id from config/data: {:?}", e);
            (String::new(), AppIdSource::DefaultEmpty)
        }
    }
}

impl ClientConfiguration {
    /// Given an (optional) set of ChannelConfigs, load all the other configs that are needed, and
    /// construct an overall configuration struct that can then be used.
    /// TODO: Move the reading of channel_configs.json into this.
    pub async fn initialize(channel_configs: Option<&ChannelConfigs>) -> Result<Self, io::Error> {
        let version = get_version()?;
        let vbmeta_data = VbMetaData::from_namespace().await.unwrap_or_else(|e| {
            warn!("Failed to get data from vbmeta {:?}", e);
            VbMetaData::default()
        });
        Ok(Self::initialize_from(&version, channel_configs, vbmeta_data).await)
    }

    async fn initialize_from(
        version: &str,
        channel_configs: Option<&ChannelConfigs>,
        VbMetaData { appid: vbmeta_appid, channel_name, realm_id, product_id, service_url }: VbMetaData,
    ) -> Self {
        let (channel_name, channel_source) = if channel_name.is_some() {
            (channel_name, ChannelSource::VbMeta)
        } else {
            // The channel wasn't found in VBMeta, so instead look for a default channel in the
            // channel configuration.
            if let Some(ChannelConfigs { default_channel: Some(name), .. }) = channel_configs {
                (Some(name.clone()), ChannelSource::Default)
            } else {
                // Channel will be loaded from `Storage` by state machine.
                (None, ChannelSource::MinFS)
            }
        };

        // Locate the channel config for the channel, if it exists.
        let channel_config = if let (Some(name), Some(configs)) = (&channel_name, channel_configs) {
            configs.get_channel(name)
        } else {
            None
        };

        let (appid, appid_source) = get_appid(vbmeta_appid, &channel_config);

        // Construct the Fuchsia system app.
        let mut extra_fields: Vec<(String, String)> =
            vec![("channel".to_string(), channel_name.clone().unwrap_or_default())];
        if let Some(product_id) = product_id {
            extra_fields.push(("product_id".to_string(), product_id));
        }
        if let Some(realm_id) = realm_id {
            extra_fields.push(("realm_id".to_string(), realm_id));
        }
        let app = App::builder()
            .id(appid.clone())
            .version(Self::parse_version(version))
            .cohort(Cohort {
                hint: channel_name.clone(),
                name: channel_name.clone(),
                ..Cohort::default()
            })
            .extra_fields(HashMap::from_iter(extra_fields.into_iter()))
            .build();
        let mut app_set = FuchsiaAppSet::new(app, AppMetadata { appid_source: appid_source });

        let eager_package_configs = EagerPackageConfigs::from_namespace();
        let platform_config = get_config(version, eager_package_configs.as_ref().ok(), service_url);

        match eager_package_configs {
            Ok(eager_package_configs) => {
                let proxy = fuchsia_component::client::connect_to_protocol::<CupMarker>()
                    .map_err(|e: Error| error!("Failed to connect to Cup protocol {:#}", e))
                    .ok();
                Self::add_eager_packages(&mut app_set, eager_package_configs, proxy).await
            }
            Err(e) => {
                match e.downcast_ref::<std::io::Error>() {
                    Some(io_err) if io_err.kind() == std::io::ErrorKind::NotFound => {
                        warn!("eager package config not found: {:#}", anyhow!(e))
                    }
                    _ => error!(
                        "Failed to load eager package config from namespace: {:#}",
                        anyhow!(e)
                    ),
                };
            }
        }

        ClientConfiguration {
            platform_config,
            app_set,
            channel_data: ChannelData {
                source: channel_source,
                name: channel_name,
                config: channel_config,
            },
        }
    }

    /// Add all eager packages in eager package config to app set.
    /// Also adds Omaha config to platform_config.
    async fn add_eager_packages(
        app_set: &mut FuchsiaAppSet,
        eager_package_configs: EagerPackageConfigs,
        cup: Option<CupProxy>,
    ) {
        for package in eager_package_configs.packages {
            let (version, channel_config) =
                Self::get_eager_package_version_and_channel(&package, &cup).await;

            let appid = match channel_config.as_ref().and_then(|c| c.appid.as_ref()) {
                Some(appid) => appid,
                None => {
                    error!("no appid for package '{}'", package.url);
                    ""
                }
            };

            let app_builder = App::builder().id(appid).version(version);
            let app = if let Some(channel_config) = channel_config {
                app_builder
                    .cohort(Cohort {
                        hint: Some(channel_config.name.clone()),
                        name: Some(channel_config.name.clone()),
                        ..Cohort::default()
                    })
                    .extra_fields([("channel".to_string(), channel_config.name.clone())])
                    .build()
            } else {
                app_builder.build()
            };

            app_set.add_eager_package(EagerPackage::new(app, Some(package.channel_config)));
        }
    }

    async fn get_eager_package_version_and_channel(
        package: &EagerPackageConfig,
        cup: &Option<CupProxy>,
    ) -> (Version, Option<ChannelConfig>) {
        let default_version = Version::from(MINIMUM_VALID_VERSION);
        if let Some(ref cup) = cup {
            match cup.get_info(&mut fpkg::PackageUrl { url: package.url.to_string() }).await {
                Ok(Ok((cup_version, cup_channel))) => {
                    let channel_config =
                        package.channel_config.get_channel(&cup_channel).or_else(|| {
                            error!(
                                "'{}' channel from CUP for package '{}' is not a known channel",
                                cup_channel, package.url
                            );
                            package.channel_config.get_default_channel()
                        });
                    let version = cup_version.parse().unwrap_or_else(|e| {
                        error!(
                            "Unable to parse '{}' as Omaha version format: {:?}",
                            cup_version, e
                        );
                        default_version
                    });
                    return (version, channel_config);
                }
                Ok(Err(GetInfoError::NotAvailable)) => {
                    info!("Eager package '{}' not currently available on the device", package.url);
                }
                Ok(Err(e)) => {
                    error!(
                        "Failed to get info about eager package '{}' from CUP: {:?}",
                        package.url, e
                    );
                }
                Err(e) => error!("Failed to send request to fuchsia.pkg.Cup: {:#}", anyhow!(e)),
            }
        }

        (default_version, package.channel_config.get_default_channel())
    }

    /// Helper to wrap the parsing of a version string, logging any parse errors and making sure
    /// that there's still some valid value as a result.
    fn parse_version(version: &str) -> Version {
        match version.parse::<Version>() {
            Ok(parsed_version) => parsed_version,
            Err(e) => {
                error!("Unable to parse '{}' as Omaha version format: {:?}", version, e);
                Version::from([0])
            }
        }
    }
}

pub fn get_config(
    version: &str,
    eager_package_configs: Option<&EagerPackageConfigs>,
    vbmeta_service_url: Option<String>,
) -> Config {
    // If eager_package_configs defines a service_url and vbmeta does too,
    // vbmeta should override the value in eager_package_configs.
    let service_url: Option<String> = vbmeta_service_url
        .or_else(|| eager_package_configs.map(|epc| epc.server.service_url.clone()));

    // This file does not exist in production, it is only used in integration/e2e testing.
    let service_url = service_url.unwrap_or_else(|| {
        fs::read_to_string("/config/data/omaha_url").unwrap_or_else(|_| {
            "https://clients2.google.com/service/update2/fuchsia/json".to_string()
        })
    });
    Config {
        updater: Updater { name: "Fuchsia".to_string(), version: Version::from([0, 0, 1, 0]) },

        os: OS {
            platform: "Fuchsia".to_string(),
            version: version.to_string(),
            service_pack: "".to_string(),
            arch: std::env::consts::ARCH.to_string(),
        },

        service_url,
        omaha_public_keys: eager_package_configs.map(|epc| epc.server.public_keys.clone()),
    }
}

pub fn get_version() -> Result<String, io::Error> {
    fs::read_to_string("/config/build-info/version").map(|s| s.trim_end().to_string())
}

#[derive(Debug, Default, Eq, PartialEq)]
struct VbMetaData {
    appid: Option<String>,
    channel_name: Option<String>,
    realm_id: Option<String>,
    product_id: Option<String>,
    service_url: Option<String>,
}

impl VbMetaData {
    async fn from_namespace() -> Result<Self, Error> {
        let proxy = fuchsia_component::client::connect_to_protocol::<ArgumentsMarker>()?;
        Self::from_proxy(proxy).await
    }
    async fn from_proxy(proxy: ArgumentsProxy) -> Result<Self, Error> {
        let keys = vec!["omaha_app_id", "ota_channel", "ota_realm", "product_id", "omaha_url"];
        let mut res = proxy.get_strings(&mut keys.into_iter()).await?;
        if res.len() != 5 {
            Err(anyhow!("Remote endpoint returned {} values, expected 5", res.len()))
        } else {
            Ok(Self {
                appid: res[0].take(),
                channel_name: res[1].take(),
                realm_id: res[2].take(),
                product_id: res[3].take(),
                service_url: res[4].take(),
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::app_set::AppMetadata;
    use eager_package_config::omaha_client::{EagerPackageConfig, OmahaServer};
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_boot::ArgumentsRequest;
    use fidl_fuchsia_pkg::CupRequest;
    use fuchsia_async as fasync;
    use fuchsia_url::UnpinnedAbsolutePackageUrl;
    use futures::prelude::*;
    use omaha_client::{
        app_set::AppSet,
        cup_ecdsa::{
            test_support::{make_default_public_key_for_test, make_default_public_keys_for_test},
            PublicKeyAndId, PublicKeys,
        },
    };
    use std::{collections::HashMap, convert::TryInto};

    #[fasync::run_singlethreaded(test)]
    async fn test_get_config() {
        let client_config =
            ClientConfiguration::initialize_from("1.2.3.4", None, VbMetaData::default()).await;
        let config = client_config.platform_config;
        assert_eq!(config.updater.name, "Fuchsia");
        let os = config.os;
        assert_eq!(os.platform, "Fuchsia");
        assert_eq!(os.version, "1.2.3.4");
        assert_eq!(os.arch, std::env::consts::ARCH);
        assert_eq!(config.service_url, "https://clients2.google.com/service/update2/fuchsia/json");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_config_service_url() {
        // If EagerPackageConfigs is present, use that service_url
        // whether or not vbmeta provides one.
        assert_eq!(
            get_config(
                "1.2.3.4",
                Some(&EagerPackageConfigs {
                    server: OmahaServer {
                        service_url: "foo".to_string(),
                        public_keys: make_default_public_keys_for_test(),
                    },
                    packages: vec![],
                }),
                /*vbmeta_service_url=*/ None,
            )
            .service_url,
            "foo"
        );
        assert_eq!(
            get_config(
                "1.2.3.4",
                Some(&EagerPackageConfigs {
                    server: OmahaServer {
                        service_url: "foo".to_string(),
                        public_keys: make_default_public_keys_for_test(),
                    },
                    packages: vec![],
                }),
                /*vbmeta_service_url=*/ Some("bar".to_string()),
            )
            .service_url,
            "bar"
        );
        // If EagerPackageConfigs is not present, use the service_url from
        // vbmeta if it is available.
        assert_eq!(
            get_config("1.2.3.4", None, /*vbmeta_service_url=*/ Some("bar".to_string()),)
                .service_url,
            "bar"
        );
        // If neither source is present, fall back to a hard-coded string used
        // in integration/e2e testing.
        assert_eq!(
            get_config("1.2.3.4", None, None,).service_url,
            "https://clients2.google.com/service/update2/fuchsia/json"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_config_read_init() {
        let config =
            ClientConfiguration::initialize_from("1.2.3.4", None, VbMetaData::default()).await;
        assert_eq!(config.channel_data.source, ChannelSource::MinFS);
        let apps = config.app_set.get_apps();
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "fuchsia:test-app-id");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, None);
        assert_eq!(apps[0].cohort.hint, None);
        assert_eq!(apps[0].extra_fields.get("product_id"), None);
        assert_eq!(apps[0].extra_fields.get("realm_id"), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_default_channel() {
        let config = ClientConfiguration::initialize_from(
            "1.2.3.4",
            Some(&ChannelConfigs {
                default_channel: Some("default-channel".to_string()),
                known_channels: vec![],
            }),
            VbMetaData::default(),
        )
        .await;
        assert_eq!(config.channel_data.source, ChannelSource::Default);
        let apps = config.app_set.get_apps();
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "fuchsia:test-app-id");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, Some("default-channel".to_string()));
        assert_eq!(apps[0].cohort.hint, Some("default-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_data_configured() {
        let channel_config = ChannelConfig::with_appid_for_test("some-channel", "some-appid");
        let channel_configs = ChannelConfigs {
            default_channel: Some(channel_config.name.clone()),
            known_channels: vec![channel_config.clone()],
        };
        let config = ClientConfiguration::initialize_from(
            "1.2.3.4",
            Some(&channel_configs),
            VbMetaData::default(),
        )
        .await;
        let channel_data = config.channel_data;

        assert_eq!(channel_data.source, ChannelSource::Default);
        assert_eq!(channel_data.name, Some("some-channel".to_string()));
        assert_eq!(channel_data.config, Some(channel_config));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_appid_from_channel_configs() {
        let config = ClientConfiguration::initialize_from(
            "1.2.3.4",
            Some(&ChannelConfigs {
                default_channel: Some("some-channel".to_string()),
                known_channels: vec![
                    ChannelConfig::new_for_test("no-appid-channel"),
                    ChannelConfig::with_appid_for_test("wrong-channel", "wrong-appid"),
                    ChannelConfig::with_appid_for_test("some-channel", "some-appid"),
                    ChannelConfig::with_appid_for_test("some-other-channel", "some-other-appid"),
                ],
            }),
            VbMetaData::default(),
        )
        .await;
        assert_eq!(config.channel_data.source, ChannelSource::Default);
        let apps = config.app_set.get_apps();
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "some-appid");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, Some("some-channel".to_string()));
        assert_eq!(apps[0].cohort.hint, Some("some-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_invalid_version() {
        let config =
            ClientConfiguration::initialize_from("invalid version", None, VbMetaData::default())
                .await;
        let apps = config.app_set.get_apps();
        assert_eq!(apps[0].version, Version::from([0]));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_init_from_vbmeta() {
        let config = ClientConfiguration::initialize_from(
            "1.2.3.4",
            Some(&ChannelConfigs {
                default_channel: Some("wrong-channel".to_string()),
                known_channels: vec![ChannelConfig::with_appid_for_test(
                    "wrong-channel",
                    "wrong-appid",
                )],
            }),
            VbMetaData {
                appid: Some("vbmeta-appid".to_string()),
                channel_name: Some("vbmeta-channel".to_string()),
                realm_id: Some("vbmeta-realm".to_string()),
                product_id: Some("vbmeta-product".to_string()),
                service_url: Some("vbmeta-url".to_string()),
            },
        )
        .await;
        assert_eq!(config.channel_data.source, ChannelSource::VbMeta);
        assert_eq!(config.channel_data.name, Some("vbmeta-channel".to_string()));
        assert_eq!(config.channel_data.config, None);
        let apps = config.app_set.get_apps();
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "vbmeta-appid");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, Some("vbmeta-channel".to_string()));
        assert_eq!(apps[0].cohort.hint, Some("vbmeta-channel".to_string()));
        assert_eq!(
            apps[0].extra_fields,
            HashMap::from([
                ("channel".to_string(), "vbmeta-channel".to_string()),
                ("product_id".to_string(), "vbmeta-product".to_string()),
                ("realm_id".to_string(), "vbmeta-realm".to_string())
            ])
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_data_from_vbmeta() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            assert_eq!(
                VbMetaData::from_proxy(proxy).await.unwrap(),
                VbMetaData {
                    appid: Some("test-appid".to_string()),
                    channel_name: Some("test-channel".to_string()),
                    realm_id: Some("test-realm".to_string()),
                    product_id: Some("test-product".to_string()),
                    service_url: Some("test-url".to_string())
                }
            );
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetStrings { keys, responder }) => {
                    assert_eq!(
                        keys,
                        vec!["omaha_app_id", "ota_channel", "ota_realm", "product_id", "omaha_url"]
                    );
                    let vec: Vec<Option<&str>> = vec![
                        Some("test-appid"),
                        Some("test-channel"),
                        Some("test-realm"),
                        Some("test-product"),
                        Some("test-url"),
                    ];
                    responder.send(&mut vec.into_iter()).expect("send failed");
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_data_from_vbmeta_missing() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            let vbmeta_data = VbMetaData::from_proxy(proxy).await.unwrap();
            assert_eq!(vbmeta_data, VbMetaData::default());
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetStrings { keys, responder }) => {
                    assert_eq!(keys.len(), 5);
                    let ret: Vec<Option<&str>> = vec![None; 5];
                    responder.send(&mut ret.into_iter()).expect("send failed");
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_data_from_vbmeta_error() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            assert!(VbMetaData::from_proxy(proxy).await.is_err());
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetStrings { .. }) => {
                    // Don't respond.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_eager_packages() {
        let platform_config = get_config("1.0.0.0", None, None);
        let system_app = App::builder().id("system_app_id").version([1]).build();
        let app_metadata = AppMetadata { appid_source: AppIdSource::VbMetadata };
        let mut app_set = FuchsiaAppSet::new(system_app.clone(), app_metadata);

        let public_keys = PublicKeys {
            latest: PublicKeyAndId {
                id: 123.try_into().unwrap(),
                key: make_default_public_key_for_test(),
            },
            historical: vec![],
        };

        assert!(platform_config.omaha_public_keys.is_none());
        let config = EagerPackageConfigs {
            server: OmahaServer {
                service_url: "https://example.com".into(),
                public_keys: public_keys.clone(),
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
                        ],
                    },
                },
                EagerPackageConfig {
                    url: UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://example.com/package2")
                        .unwrap(),
                    flavor: None,
                    channel_config: ChannelConfigs {
                        default_channel: None,
                        known_channels: vec![ChannelConfig {
                            name: "stable".into(),
                            repo: "stable".into(),
                            appid: Some("3c4d5e6f".into()),
                            check_interval_secs: None,
                        }],
                    },
                },
            ],
        };
        // without CUP
        ClientConfiguration::add_eager_packages(&mut app_set, config.clone(), None).await;

        // add_eager_packages does not set public keys; get_config does.
        assert!(platform_config.omaha_public_keys.is_none());

        let package_app = App::builder()
            .id("1a2b3c4d")
            .version(MINIMUM_VALID_VERSION)
            .cohort(Cohort {
                hint: Some("stable".into()),
                name: Some("stable".into()),
                ..Cohort::default()
            })
            .extra_fields([("channel".to_string(), "stable".to_string())])
            .build();
        let package2_app = App::builder().id("").version(MINIMUM_VALID_VERSION).build();
        assert_eq!(app_set.get_apps(), vec![system_app.clone(), package_app, package2_app]);

        // now with CUP
        let app_metadata = AppMetadata { appid_source: AppIdSource::VbMetadata };
        let mut app_set = FuchsiaAppSet::new(system_app.clone(), app_metadata);
        let (proxy, mut stream) = create_proxy_and_stream::<CupMarker>().unwrap();
        let stream_fut = async move {
            while let Some(request) = stream.next().await {
                match request {
                    Ok(CupRequest::GetInfo { url, responder }) => {
                        let response = match url.url.as_str() {
                            "fuchsia-pkg://example.com/package" => ("1.2.3".into(), "beta".into()),
                            "fuchsia-pkg://example.com/package2" => {
                                ("4.5.6".into(), "stable".into())
                            }
                            url => panic!("unexpected url {}", url),
                        };
                        responder.send(&mut Ok(response)).unwrap();
                    }
                    request => panic!("Unexpected request: {:?}", request),
                }
            }
        };
        let fut = ClientConfiguration::add_eager_packages(&mut app_set, config, Some(proxy));
        future::join(fut, stream_fut).await;
        let package_app = App::builder()
            .id("1a2b3c4d")
            .version([1, 2, 3, 0])
            .cohort(Cohort {
                hint: Some("beta".into()),
                name: Some("beta".into()),
                ..Cohort::default()
            })
            .extra_fields([("channel".to_string(), "beta".to_string())])
            .build();
        let package2_app = App::builder()
            .id("3c4d5e6f")
            .version([4, 5, 6, 0])
            .cohort(Cohort {
                hint: Some("stable".into()),
                name: Some("stable".into()),
                ..Cohort::default()
            })
            .extra_fields([("channel".to_string(), "stable".to_string())])
            .build();
        assert_eq!(app_set.get_apps(), vec![system_app, package_app, package2_app]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_eager_package_version_and_channel_fallback() {
        let (proxy, mut stream) = create_proxy_and_stream::<CupMarker>().unwrap();
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(CupRequest::GetInfo { url, responder }) => {
                    assert_eq!(url.url, "fuchsia-pkg://example.com/package");
                    responder.send(&mut Ok(("abc".into(), "beta".into()))).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        let stable_channel_config = ChannelConfig {
            name: "stable".into(),
            repo: "stable".into(),
            appid: Some("1a2b3c4d".into()),
            check_interval_secs: None,
        };
        let config = EagerPackageConfig {
            url: UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://example.com/package").unwrap(),
            flavor: Some("debug".into()),
            channel_config: ChannelConfigs {
                default_channel: Some("stable".into()),
                known_channels: vec![stable_channel_config.clone()],
            },
        };
        // unknown channel or invalid version fallback to default
        let ((version, channel_config), ()) = future::join(
            ClientConfiguration::get_eager_package_version_and_channel(&config, &Some(proxy)),
            stream_fut,
        )
        .await;
        assert_eq!(channel_config.unwrap(), stable_channel_config);
        assert_eq!(version, MINIMUM_VALID_VERSION.into());

        // GetInfoError fallback to default
        let (proxy, mut stream) = create_proxy_and_stream::<CupMarker>().unwrap();
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(CupRequest::GetInfo { url, responder }) => {
                    assert_eq!(url.url, "fuchsia-pkg://example.com/package");
                    responder.send(&mut Err(GetInfoError::NotAvailable)).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        let ((version, channel_config), ()) = future::join(
            ClientConfiguration::get_eager_package_version_and_channel(&config, &Some(proxy)),
            stream_fut,
        )
        .await;
        assert_eq!(channel_config.unwrap(), stable_channel_config);
        assert_eq!(version, MINIMUM_VALID_VERSION.into());

        // no proxy fallback to default
        let (version, channel_config) =
            ClientConfiguration::get_eager_package_version_and_channel(&config, &None).await;
        assert_eq!(channel_config.unwrap(), stable_channel_config);
        assert_eq!(version, MINIMUM_VALID_VERSION.into());
    }
}
