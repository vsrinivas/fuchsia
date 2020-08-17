// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::channel::ChannelConfigs;
use anyhow::{anyhow, Error};
use fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsProxy};
use log::{error, warn};
use omaha_client::{
    common::{App, AppSet, Version},
    configuration::{Config, Updater},
    protocol::{request::OS, Cohort},
};
use std::fs;
use std::io;

/// The source of the channel configuration.
#[derive(Debug, Eq, PartialEq)]
pub enum ChannelSource {
    MinFS,
    Default,
    VbMeta,
}

pub async fn get_app_set(
    version: &str,
    channel_configs: &Option<ChannelConfigs>,
) -> (AppSet, ChannelSource) {
    let (appid, mut channel) = get_appid_and_channel_from_vbmeta().await.unwrap_or_else(|e| {
        warn!("Failed to get app id and channel from vbmeta {:?}", e);
        (None, None)
    });
    let channel_source = if channel.is_some() {
        ChannelSource::VbMeta
    } else {
        channel = channel_configs.as_ref().and_then(|configs| configs.default_channel.clone());
        if channel.is_some() {
            ChannelSource::Default
        } else {
            // Channel will be loaded from `Storage` by state machine.
            ChannelSource::MinFS
        }
    };
    // If no appid in vbmeta, look up the appid of the channel from channel configs.
    let appid = appid.or_else(|| {
        channel_configs.as_ref().and_then(|configs| {
            channel.as_ref().and_then(|channel| {
                configs
                    .known_channels
                    .iter()
                    .find(|c| &c.name == channel)
                    .and_then(|c| c.appid.clone())
            })
        })
    });
    let id = appid.unwrap_or_else(|| match fs::read_to_string("/config/data/omaha_app_id") {
        Ok(id) => id,
        Err(e) => {
            error!("Unable to read omaha app id from config/data: {:?}", e);
            String::new()
        }
    });
    let version = match version.parse::<Version>() {
        Ok(version) => version,
        Err(e) => {
            error!("Unable to parse '{}' as Omaha version format: {:?}", version, e);
            Version::from([0])
        }
    };
    (
        // Fuchsia only has a single app.
        AppSet::new(vec![App::builder(id, version)
            .with_cohort(Cohort {
                hint: channel.clone(),
                name: channel.clone(),
                ..Cohort::default()
            })
            .with_extra("channel", channel.unwrap_or("".to_string()))
            .build()]),
        channel_source,
    )
}

pub async fn get_config(version: &str) -> Config {
    // This file does not exist in production, it is only used in integration/e2e testing.
    let service_url = match get_service_url_from_vbmeta().await {
        Ok(Some(url)) => url,
        _ => fs::read_to_string("/config/data/omaha_url")
            .unwrap_or("https://clients2.google.com/service/update2/fuchsia/json".to_string()),
    };
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

async fn get_appid_and_channel_from_vbmeta() -> Result<(Option<String>, Option<String>), Error> {
    let proxy = fuchsia_component::client::connect_to_service::<ArgumentsMarker>()?;
    get_appid_and_channel_from_vbmeta_impl(proxy).await
}

async fn get_appid_and_channel_from_vbmeta_impl(
    proxy: ArgumentsProxy,
) -> Result<(Option<String>, Option<String>), Error> {
    let vec = vec!["omaha_app_id", "ota_channel"];
    let res = proxy.get_strings(&mut vec.into_iter()).await?;
    if res.len() != 2 {
        Err(anyhow!("Remote endpoint returned {} values, expected 2", res.len()))
    } else {
        Ok((res[0].clone(), res[1].clone()))
    }
}

async fn get_service_url_from_vbmeta() -> Result<Option<String>, Error> {
    let proxy = fuchsia_component::client::connect_to_service::<ArgumentsMarker>()?;
    get_service_url_from_vbmeta_impl(proxy).await
}

async fn get_service_url_from_vbmeta_impl(proxy: ArgumentsProxy) -> Result<Option<String>, Error> {
    Ok(proxy.get_string("omaha_url").await?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::channel::ChannelConfig;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_boot::ArgumentsRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_get_config() {
        let config = get_config("1.2.3.4").await;
        assert_eq!(config.updater.name, "Fuchsia");
        let os = config.os;
        assert_eq!(os.platform, "Fuchsia");
        assert_eq!(os.version, "1.2.3.4");
        assert_eq!(os.arch, std::env::consts::ARCH);
        assert_eq!(config.service_url, "https://clients2.google.com/service/update2/fuchsia/json");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set() {
        let (app_set, channel_source) = get_app_set("1.2.3.4", &None).await;
        assert_eq!(channel_source, ChannelSource::MinFS);
        let apps = app_set.to_vec().await;
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "fuchsia:test-app-id");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, None);
        assert_eq!(apps[0].cohort.hint, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_default_channel() {
        let (app_set, channel_source) = get_app_set(
            "1.2.3.4",
            &Some(ChannelConfigs {
                default_channel: Some("default-channel".to_string()),
                known_channels: vec![],
            }),
        )
        .await;
        assert_eq!(channel_source, ChannelSource::Default);
        let apps = app_set.to_vec().await;
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "fuchsia:test-app-id");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, Some("default-channel".to_string()));
        assert_eq!(apps[0].cohort.hint, Some("default-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_appid_from_channel_configs() {
        let (app_set, channel_source) = get_app_set(
            "1.2.3.4",
            &Some(ChannelConfigs {
                default_channel: Some("some-channel".to_string()),
                known_channels: vec![
                    ChannelConfig::new("no-appid-channel"),
                    ChannelConfig::with_appid("wrong-channel", "wrong-appid"),
                    ChannelConfig::with_appid("some-channel", "some-appid"),
                    ChannelConfig::with_appid("some-other-channel", "some-other-appid"),
                ],
            }),
        )
        .await;
        assert_eq!(channel_source, ChannelSource::Default);
        let apps = app_set.to_vec().await;
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "some-appid");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, Some("some-channel".to_string()));
        assert_eq!(apps[0].cohort.hint, Some("some-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_invalid_version() {
        let (app_set, _) = get_app_set("invalid version", &None).await;
        let apps = app_set.to_vec().await;
        assert_eq!(apps[0].version, Version::from([0]));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_appid_and_channel_from_vbmeta() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            let (appid, channel) = get_appid_and_channel_from_vbmeta_impl(proxy).await.unwrap();
            assert_eq!(appid, Some("test-appid".to_string()));
            assert_eq!(channel, Some("test-channel".to_string()));
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetStrings { keys, responder }) => {
                    assert_eq!(keys, vec!["omaha_app_id", "ota_channel"]);
                    let vec: Vec<Option<&str>> = vec![Some("test-appid"), Some("test-channel")];
                    responder.send(&mut vec.into_iter()).expect("send failed");
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_appid_and_channel_from_vbmeta_missing() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            let (appid, channel) = get_appid_and_channel_from_vbmeta_impl(proxy).await.unwrap();
            assert_eq!(appid, None);
            assert_eq!(channel, None);
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetStrings { keys, responder }) => {
                    assert_eq!(keys.len(), 2);
                    let ret: Vec<Option<&str>> = vec![None, None];
                    responder.send(&mut ret.into_iter()).expect("send failed");
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_appid_and_channel_from_vbmeta_error() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            assert!(get_appid_and_channel_from_vbmeta_impl(proxy).await.is_err());
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
    async fn test_get_service_url_from_vbmeta() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            let url = get_service_url_from_vbmeta_impl(proxy).await.unwrap();
            assert_eq!(url, Some("test-url".to_string()));
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetString { key, responder }) => {
                    assert_eq!(key, "omaha_url");
                    responder.send(Some("test-url")).expect("send failed");
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_service_url_from_vbmeta_missing() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            let url = get_service_url_from_vbmeta_impl(proxy).await.unwrap();
            assert_eq!(url, None);
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetString { key, responder }) => {
                    assert_eq!(key, "omaha_url");
                    responder.send(None).expect("send failed");
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_service_url_from_vbmeta_error() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let fut = async move {
            assert!(get_service_url_from_vbmeta_impl(proxy).await.is_err());
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetString { .. }) => {
                    // Don't respond.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }
}
