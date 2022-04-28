// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::channel::{ChannelConfig, ChannelConfigs};
use anyhow::{anyhow, Error};
use fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsProxy};
use log::{error, warn};
use omaha_client::{
    common::{App, AppSet},
    configuration::{Config, Updater},
    protocol::{request::OS, Cohort},
};
use std::fs;
use std::io;
use version::Version;

/// This struct is the overall "configuration" of the omaha client.  Minus the PolicyConfig.  That
/// should probably be included in here as well, eventually.
pub struct ClientConfiguration {
    pub platform_config: omaha_client::configuration::Config,
    pub app_set: AppSet,
    pub channel_data: ChannelData,
}

/// This wraps up all the channel-related configuration (name, appid, config, and any other params
/// that they might have).  Mostly as a grouping for returning from fns in a cleaner way than a
/// tuple provides.
pub struct ChannelData {
    pub source: ChannelSource,
    pub name: Option<String>,
    pub config: Option<ChannelConfig>,
    pub appid: String,
}

/// The source of the channel configuration.
#[derive(Debug, Eq, PartialEq)]
pub enum ChannelSource {
    MinFS,
    Default,
    VbMeta,
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
        VbMetaData { appid, channel_name, realm_id, product_id, service_url }: VbMetaData,
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

        // If no appid in vbmeta, look up the appid of the channel from the channel config.
        let appid = if let (None, Some(config)) = (&appid, &channel_config) {
            config.appid.clone()
        } else {
            appid
        };

        // If no appid in the channel configs, then attempt to read from config data.
        let appid =
            appid.unwrap_or_else(|| match fs::read_to_string("/config/data/omaha_app_id") {
                Ok(id) => id,
                Err(e) => {
                    error!("Unable to read omaha app id from config/data: {:?}", e);
                    String::new()
                }
            });

        // Construct the Fuchsia system app.
        let mut app_builder = App::builder(appid.clone(), Self::parse_version(version))
            .with_cohort(Cohort {
                hint: channel_name.clone(),
                name: channel_name.clone(),
                ..Cohort::default()
            })
            .with_extra("channel", channel_name.clone().unwrap_or_default());
        if let Some(product_id) = product_id {
            app_builder = app_builder.with_extra("product_id", product_id);
        }
        if let Some(realm_id) = realm_id {
            app_builder = app_builder.with_extra("realm_id", realm_id);
        }
        let app = app_builder.build();
        let app_set = AppSet::new(vec![app]);

        ClientConfiguration {
            platform_config: get_config(&version, service_url),
            app_set,
            channel_data: ChannelData {
                source: channel_source,
                name: channel_name,
                config: channel_config,
                appid,
            },
        }
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

pub fn get_config(version: &str, vbmeta_service_url: Option<String>) -> Config {
    // This file does not exist in production, it is only used in integration/e2e testing.
    let service_url = vbmeta_service_url.unwrap_or_else(|| {
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
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_boot::ArgumentsRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use std::collections::HashMap;

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
    async fn test_get_app_set_config_read_init() {
        let config =
            ClientConfiguration::initialize_from("1.2.3.4", None, VbMetaData::default()).await;
        assert_eq!(config.channel_data.source, ChannelSource::MinFS);
        let apps = config.app_set.to_vec().await;
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
        let apps = config.app_set.to_vec().await;
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "fuchsia:test-app-id");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, Some("default-channel".to_string()));
        assert_eq!(apps[0].cohort.hint, Some("default-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_data_configured() {
        let channel_config = ChannelConfig::with_appid("some-channel", "some-appid");
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
        assert_eq!(channel_data.appid, "some-appid");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_appid_from_channel_configs() {
        let config = ClientConfiguration::initialize_from(
            "1.2.3.4",
            Some(&ChannelConfigs {
                default_channel: Some("some-channel".to_string()),
                known_channels: vec![
                    ChannelConfig::new("no-appid-channel"),
                    ChannelConfig::with_appid("wrong-channel", "wrong-appid"),
                    ChannelConfig::with_appid("some-channel", "some-appid"),
                    ChannelConfig::with_appid("some-other-channel", "some-other-appid"),
                ],
            }),
            VbMetaData::default(),
        )
        .await;
        assert_eq!(config.channel_data.source, ChannelSource::Default);
        let apps = config.app_set.to_vec().await;
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
        let apps = config.app_set.to_vec().await;
        assert_eq!(apps[0].version, Version::from([0]));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_init_from_vbmeta() {
        let config = ClientConfiguration::initialize_from(
            "1.2.3.4",
            Some(&ChannelConfigs {
                default_channel: Some("wrong-channel".to_string()),
                known_channels: vec![ChannelConfig::with_appid("wrong-channel", "wrong-appid")],
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
        assert_eq!(config.channel_data.appid, "vbmeta-appid");
        let apps = config.app_set.to_vec().await;
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
}
