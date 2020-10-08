// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_sys::{self as sys, LeSecurityMode},
    serde::{Deserialize, Serialize},
    serde_json,
    std::{cmp::PartialEq, convert::Into, fs::OpenOptions, path::Path},
};

static OVERRIDE_CONFIG_FILE_PATH: &'static str = "/config/data/build-config.json";
static DEFAULT_CONFIG_FILE_PATH: &'static str = "/pkg/data/default.json";

/// The `build_config` module enables build-time configuration of the Bluetooth Host Subsystem.
/// Default configuration parameters are taken from //src/connectivity/bluetooth/core/bt-gap/
/// config/default.json. `build_config` also enables overriding the default configuration without
/// changing the Fuchsia source tree through the `config-data` component sandbox feature and
/// associated `config_data` GN template with the arguments:
/// ```
///     for_pkg = "bt-gap",
///     outputs = [
///         "build-config.json"
///     ]
/// ``` (https://fuchsia.dev/fuchsia-src/development/components/config_data).
/// `build_config::Config` clients access the configuration settings through the `load_default`
/// free function.
#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct Config {
    pub le: LeConfig,
    pub bredr: BrEdrConfig,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct LeConfig {
    #[serde(rename = "privacy-enabled")]
    pub privacy_enabled: bool,
    #[serde(rename = "background-scan-enabled")]
    pub background_scan_enabled: bool,
    #[serde(rename = "security-mode")]
    #[serde(with = "LeSecurityModeDef")]
    pub security_mode: LeSecurityMode,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct BrEdrConfig {
    pub connectable: bool,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "LeSecurityMode")]
pub enum LeSecurityModeDef {
    Mode1 = 1,
    SecureConnectionsOnly = 2,
}

impl Config {
    pub fn update_with_sys_settings(&self, new_settings: &sys::Settings) -> Self {
        let mut new_config = self.clone();
        new_config.le.privacy_enabled = new_settings.le_privacy.unwrap_or(self.le.privacy_enabled);
        new_config.le.background_scan_enabled =
            new_settings.le_background_scan.unwrap_or(self.le.background_scan_enabled);
        new_config.le.security_mode =
            new_settings.le_security_mode.unwrap_or(self.le.security_mode);
        new_config.bredr.connectable =
            new_settings.bredr_connectable_mode.unwrap_or(self.bredr.connectable);
        new_config
    }
}

impl Into<sys::Settings> for Config {
    fn into(self) -> sys::Settings {
        sys::Settings {
            le_privacy: Some(self.le.privacy_enabled),
            le_background_scan: Some(self.le.background_scan_enabled),
            bredr_connectable_mode: Some(self.bredr.connectable),
            le_security_mode: Some(self.le.security_mode),
        }
    }
}

pub fn load_default() -> Config {
    load_internal(Path::new(OVERRIDE_CONFIG_FILE_PATH), Path::new(DEFAULT_CONFIG_FILE_PATH))
}

fn load_internal(override_file_path: &Path, default_file_path: &Path) -> Config {
    let config_path =
        if override_file_path.exists() { override_file_path } else { default_file_path };
    let config_file = OpenOptions::new().read(true).write(false).open(config_path).unwrap();
    serde_json::from_reader(config_file)
        .expect("Malformatted bt-gap config file, cannot initialize Bluetooth stack")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host_device::HostDevice;
    use {
        fidl::encoding::Decodable,
        fidl_fuchsia_bluetooth_host::{HostMarker, HostRequest},
        fuchsia_bluetooth::types::{Address, HostId},
        futures::{future, join, stream::TryStreamExt},
        matches::assert_matches,
        std::collections::HashSet,
        tempfile::NamedTempFile,
    };

    static BASIC_CONFIG: Config = Config {
        le: LeConfig {
            privacy_enabled: true,
            background_scan_enabled: true,
            security_mode: LeSecurityMode::Mode1,
        },
        bredr: BrEdrConfig { connectable: true },
    };

    #[test]
    fn prefer_overridden_config() {
        let default_file = NamedTempFile::new().unwrap();
        serde_json::to_writer(&default_file, &BASIC_CONFIG).unwrap();
        // There should be no file at OVERRIDE_CONFIG_FILE_PATH; `config_data` templates should not
        // target this test package. This means config will load from the (existing) default path.
        assert_eq!(
            BASIC_CONFIG,
            load_internal(Path::new(OVERRIDE_CONFIG_FILE_PATH), default_file.path())
        );

        // Write a different config file and set it as the override location to verify that `load`
        // picks up and prefers the override file.
        let override_config = Config {
            le: LeConfig {
                privacy_enabled: true,
                background_scan_enabled: false,
                security_mode: LeSecurityMode::SecureConnectionsOnly,
            },
            bredr: BrEdrConfig { connectable: false },
        };
        let override_file = NamedTempFile::new().unwrap();
        serde_json::to_writer(&override_file, &override_config).unwrap();
        assert_eq!(override_config, load_internal(override_file.path(), default_file.path()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn apply_config() {
        let (host_proxy, host_server) =
            fidl::endpoints::create_proxy_and_stream::<HostMarker>().unwrap();
        let host_device = HostDevice::mock(
            HostId(42),
            Address::Public([1, 2, 3, 4, 5, 6]),
            Path::new("/dev/host"),
            host_proxy,
        );
        let test_config = Config {
            le: LeConfig {
                privacy_enabled: true,
                background_scan_enabled: false,
                security_mode: LeSecurityMode::Mode1,
            },
            bredr: BrEdrConfig { connectable: false },
        };
        let run_host = async {
            let mut expected_reqs = HashSet::<String>::new();
            expected_reqs.insert("enable_privacy".into());
            expected_reqs.insert("enable_background_scan".into());
            expected_reqs.insert("set_connectable".into());
            expected_reqs.insert("set_le_security_mode".into());
            host_server
                .try_for_each(|req| {
                    match req {
                        HostRequest::EnablePrivacy { enabled, .. } => {
                            assert!(expected_reqs.remove("enable_privacy"));
                            assert_eq!(test_config.le.privacy_enabled, enabled);
                        }
                        HostRequest::EnableBackgroundScan { enabled, .. } => {
                            assert!(expected_reqs.remove("enable_background_scan"));
                            assert_eq!(test_config.le.background_scan_enabled, enabled);
                        }
                        HostRequest::SetConnectable { enabled, responder } => {
                            assert!(expected_reqs.remove("set_connectable"));
                            assert_eq!(test_config.bredr.connectable, enabled);
                            assert_matches!(responder.send(&mut Ok(())), Ok(()));
                        }
                        HostRequest::SetLeSecurityMode { le_security_mode, .. } => {
                            assert!(expected_reqs.remove("set_le_security_mode"));
                            assert_eq!(test_config.le.security_mode, le_security_mode);
                        }
                        _ => panic!("unexpected HostRequest!"),
                    };
                    future::ok(())
                })
                .await
                .unwrap();
            assert_eq!(expected_reqs.into_iter().collect::<Vec<String>>(), Vec::<String>::new());
        };
        let apply_config = async {
            host_device.apply_config(test_config.clone()).await.unwrap();
            // Drop `host_device` so the host server request stream terminates
            drop(host_device)
        };
        join!(run_host, apply_config);
    }

    #[test]
    fn update_with_sys_settings() {
        let partial_settings = sys::Settings {
            le_privacy: Some(false),
            bredr_connectable_mode: Some(false),
            ..sys::Settings::new_empty()
        };
        let expected_config = Config {
            le: LeConfig { privacy_enabled: false, ..BASIC_CONFIG.le },
            bredr: BrEdrConfig { connectable: false, ..BASIC_CONFIG.bredr },
        };
        assert_eq!(
            expected_config,
            BASIC_CONFIG.clone().update_with_sys_settings(&partial_settings)
        );
    }
}
