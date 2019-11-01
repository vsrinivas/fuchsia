// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::{error, info};
use omaha_client::{
    common::{App, AppSet, UserCounting, Version},
    configuration::{Config, Updater},
    protocol::{request::OS, Cohort},
};
use std::fs;
use std::io;

/// The source of the channel configuration.
#[derive(Debug, Eq, PartialEq)]
pub enum ChannelSource {
    MinFS,
    SysConfig,
    Default,
}

pub fn get_app_set(version: &str, default_channel: Option<String>) -> (AppSet, ChannelSource) {
    let id = match fs::read_to_string("/config/data/omaha_app_id") {
        Ok(id) => id,
        Err(e) => {
            error!("Unable read omaha app id: {:?}", e);
            String::new()
        }
    };
    let version = match version.parse::<Version>() {
        Ok(version) => version,
        Err(e) => {
            error!("Unable to parse '{}' as Omaha version format: {:?}", version, e);
            Version::from([0])
        }
    };
    let channel_config = sysconfig_client::channel::read_channel_config();
    info!("Channel configuration in sysconfig: {:?}", channel_config);
    let mut channel = channel_config.map(|config| config.channel_name().to_string()).ok();
    let channel_source = if channel.is_some() {
        ChannelSource::SysConfig
    } else {
        channel = default_channel;
        if channel.is_some() {
            ChannelSource::Default
        } else {
            // Channel will be loaded from `Storage` by state machine.
            ChannelSource::MinFS
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

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

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
        let (app_set, channel_source) = get_app_set("1.2.3.4", None);
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
        let (app_set, channel_source) = get_app_set("1.2.3.4", Some("default-channel".to_string()));
        assert_eq!(channel_source, ChannelSource::Default);
        let apps = app_set.to_vec().await;
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "fuchsia:test-app-id");
        assert_eq!(apps[0].version, Version::from([1, 2, 3, 4]));
        assert_eq!(apps[0].cohort.name, Some("default-channel".to_string()));
        assert_eq!(apps[0].cohort.hint, Some("default-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_app_set_invalid_version() {
        let (app_set, _) = get_app_set("invalid version", None);
        let apps = app_set.to_vec().await;
        assert_eq!(apps[0].version, Version::from([0]));
    }
}
