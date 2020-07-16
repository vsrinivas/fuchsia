// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    parking_lot::RwLock,
    serde::{Deserialize, Serialize},
    serde_json,
    std::{cmp::PartialEq, fs::OpenOptions, path::Path, sync::Arc},
};

use crate::{host_device::HostDevice, types};

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
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct BrEdrConfig {
    pub connectable: bool,
}

impl Config {
    pub async fn apply(&self, host: Arc<RwLock<HostDevice>>) -> types::Result<()> {
        let host = host.read();
        host.enable_privacy(self.le.privacy_enabled)?;
        host.enable_background_scan(self.le.background_scan_enabled)?;
        host.set_connectable(self.bredr.connectable).await?;
        Ok(())
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
    use crate::host_device;
    use {
        fidl_fuchsia_bluetooth_host::{HostMarker, HostRequest},
        fuchsia_bluetooth::types::{Address, HostId},
        futures::{future, join, stream::TryStreamExt},
        matches::assert_matches,
        std::collections::HashSet,
        tempfile::NamedTempFile,
    };

    #[test]
    fn prefer_overridden_config() {
        let default_config = Config {
            le: LeConfig { privacy_enabled: true, background_scan_enabled: true },
            bredr: BrEdrConfig { connectable: true },
        };
        let default_file = NamedTempFile::new().unwrap();
        serde_json::to_writer(&default_file, &default_config).unwrap();
        // There should be no file at OVERRIDE_CONFIG_FILE_PATH; `config_data` templates should not
        // target this test package. This means config will load from the (existing) default path.
        assert_eq!(
            default_config,
            load_internal(Path::new(OVERRIDE_CONFIG_FILE_PATH), default_file.path())
        );

        // Write a different config file and set it as the override location to verify that `load`
        // picks up and prefers the override file.
        let override_config = Config {
            le: LeConfig { privacy_enabled: true, background_scan_enabled: false },
            bredr: BrEdrConfig { connectable: false },
        };
        let override_file = NamedTempFile::new().unwrap();
        serde_json::to_writer(&override_file, &override_config).unwrap();
        assert_eq!(override_config, load_internal(override_file.path(), default_file.path()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_apply_config() {
        let (host_proxy, host_server) =
            fidl::endpoints::create_proxy_and_stream::<HostMarker>().unwrap();
        let host_device = Arc::new(RwLock::new(host_device::test::new_mock(
            HostId(42),
            Address::Public([1, 2, 3, 4, 5, 6]),
            Path::new("/dev/host"),
            host_proxy,
        )));
        let test_config = Config {
            le: LeConfig { privacy_enabled: true, background_scan_enabled: false },
            bredr: BrEdrConfig { connectable: false },
        };
        let run_host = async {
            let mut expected_reqs = HashSet::<String>::new();
            expected_reqs.insert("enable_privacy".into());
            expected_reqs.insert("enable_background_scan".into());
            expected_reqs.insert("set_connectable".into());
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
                        _ => panic!("unexpected HostRequest!"),
                    };
                    future::ok(())
                })
                .await
                .unwrap();
            assert_eq!(expected_reqs.into_iter().collect::<Vec<String>>(), Vec::<String>::new());
        };
        let apply_config = async {
            test_config.apply(host_device).await.unwrap();
        };
        join!(run_host, apply_config);
    }
}
