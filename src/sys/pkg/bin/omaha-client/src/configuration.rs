// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::channel::ChannelConfigs;
use anyhow::Error;
use fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsProxy};
use log::{error, info, warn};
use omaha_client::{
    common::{App, AppSet, UserCounting, Version},
    configuration::{Config, Updater},
    protocol::{request::OS, Cohort},
};
use std::fs;
use std::io;

#[cfg(not(test))]
use sysconfig_client::channel::read_channel_config;

#[cfg(test)]
use sysconfig_mock::read_channel_config;

/// The source of the channel configuration.
#[derive(Debug, Eq, PartialEq)]
pub enum ChannelSource {
    MinFS,
    SysConfig,
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
        let sysconfig_channel_config = read_channel_config();
        info!("Channel configuration in sysconfig: {:?}", sysconfig_channel_config);
        channel = sysconfig_channel_config.map(|config| config.channel_name().to_string()).ok();
        if channel.is_some() {
            ChannelSource::SysConfig
        } else {
            channel = channel_configs.as_ref().and_then(|configs| configs.default_channel.clone());
            if channel.is_some() {
                ChannelSource::Default
            } else {
                // Channel will be loaded from `Storage` by state machine.
                ChannelSource::MinFS
            }
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
    let cohort = Cohort { hint: channel.clone(), name: channel, ..Cohort::default() };
    (
        // Fuchsia only has a single app.
        AppSet::new(vec![App {
            id,
            version,
            fingerprint: None,
            cohort,
            user_counting: UserCounting::ClientRegulatedByDate(None),
        }]),
        channel_source,
    )
}

pub fn get_config(version: &str) -> Config {
    Config {
        updater: Updater { name: "Fuchsia".to_string(), version: Version::from([0, 0, 1, 0]) },

        os: OS {
            platform: "Fuchsia".to_string(),
            version: version.to_string(),
            service_pack: "".to_string(),
            arch: std::env::consts::ARCH.to_string(),
        },

        service_url: "https://clients2.google.com/service/update2/fuchsia/json".to_string(),
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
    let mut appid = None;
    let mut channel = None;
    let (vmo, size) = proxy.get().await?;
    let mut buf = vec![0u8; size as usize];
    vmo.read(&mut buf, 0)?;
    for arg in buf.split(|&byte| byte == 0) {
        let arg = String::from_utf8_lossy(arg);
        let appid_prefix = "omaha.appid=";
        if arg.starts_with(appid_prefix) {
            appid = Some(arg[appid_prefix.len()..].to_string());
            continue;
        }
        let channel_prefix = "omaha.channel=";
        if arg.starts_with(channel_prefix) {
            channel = Some(arg[channel_prefix.len()..].to_string());
        }
    }
    Ok((appid, channel))
}

#[cfg(test)]
mod sysconfig_mock {
    use std::cell::RefCell;
    use sysconfig_client::channel::{ChannelConfigError, OtaUpdateChannelConfig};

    thread_local! {
        static MOCK_RESULT: RefCell<Result<OtaUpdateChannelConfig, ChannelConfigError>> =
            RefCell::new(Err(ChannelConfigError::Magic(0)));
    }

    pub(super) fn read_channel_config() -> Result<OtaUpdateChannelConfig, ChannelConfigError> {
        MOCK_RESULT.with(|result| result.replace(Err(ChannelConfigError::Magic(0))))
    }

    pub(super) fn set_read_channel_config_result(
        new_result: Result<OtaUpdateChannelConfig, ChannelConfigError>,
    ) {
        MOCK_RESULT.with(|result| *result.borrow_mut() = new_result);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::channel::ChannelConfig;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_boot::ArgumentsRequest;
    use fuchsia_async as fasync;
    use fuchsia_zircon::Vmo;
    use futures::prelude::*;
    use sysconfig_client::channel::OtaUpdateChannelConfig;

    #[test]
    fn test_get_config() {
        let config = get_config("1.2.3.4");
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
    async fn test_get_app_set_appid_from_channel_configs_sysconfig() {
        sysconfig_mock::set_read_channel_config_result(OtaUpdateChannelConfig::new(
            "some-channel",
            "some-repo",
        ));
        let (app_set, channel_source) = get_app_set(
            "1.2.3.4",
            &Some(ChannelConfigs {
                default_channel: Some("some-other-channel".to_string()),
                known_channels: vec![
                    ChannelConfig::new("no-appid-channel"),
                    ChannelConfig::with_appid("wrong-channel", "wrong-appid"),
                    ChannelConfig::with_appid("some-channel", "some-appid"),
                    ChannelConfig::with_appid("some-other-channel", "some-other-appid"),
                ],
            }),
        )
        .await;
        assert_eq!(channel_source, ChannelSource::SysConfig);
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
                Ok(ArgumentsRequest::Get { responder }) => {
                    let args = b"foo=bar\0omaha.appid=test-appid\0omaha.channel=test-channel\0";
                    let size = args.len() as u64;
                    let vmo = Vmo::create(size).unwrap();
                    vmo.write(args, 0).unwrap();
                    responder.send(vmo, size).unwrap();
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
                Ok(ArgumentsRequest::Get { responder }) => {
                    let args = b"foo=bar\0";
                    let size = args.len() as u64;
                    let vmo = Vmo::create(size).unwrap();
                    vmo.write(args, 0).unwrap();
                    responder.send(vmo, size).unwrap();
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
                Ok(ArgumentsRequest::Get { .. }) => {
                    // Don't respond.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }
}
