// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Error},
    fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsProxy},
    fidl_fuchsia_buildinfo::{ProviderMarker as BuildInfoMarker, ProviderProxy as BuildInfoProxy},
    serde::{Deserialize, Serialize},
    std::convert::From,
    std::{fs::File, io::BufReader},
};

const PATH_TO_RECOVERY_CONFIG: &'static str = "/config/data/recovery-config.json";
const DEFAULT_OMAHA_SERVICE_URL: &'static str =
    "https://clients2.google.com/service/update2/fuchsia/json";

#[derive(Debug, PartialEq)]
pub struct RecoveryUpdateConfig {
    pub channel: String,
    pub update_type: UpdateType,
    pub version: String,
}

#[derive(Debug, PartialEq)]
pub enum UpdateType {
    /// Designates an Omaha based update
    /// Parameters:
    ///     app_id: The omaha application id
    ///     omaha_service_url: the omaha service url query for updates
    Omaha { app_id: String, service_url: String },
    /// Designates a TUF based update
    Tuf,
}

impl RecoveryUpdateConfig {
    /// Resolve the update configuration using values from vbmeta (via boot args), the
    /// build-provided json config, and the current version from BuildInfo.
    pub async fn resolve_update_config() -> Result<RecoveryUpdateConfig, Error> {
        let json_config =
            JsonUpdateConfig::load_json_config_data().context("Failed to load json config data")?;

        let boot_args = {
            let arguments_proxy =
                fuchsia_component::client::connect_to_protocol::<ArgumentsMarker>()
                    .context("Could not load boot arguments.")?;
            BootloaderArgs::load_from_proxy(arguments_proxy)
                .await
                .context("Unable to connect to fuchsia.boot.Arguments.")
        }
        .map_err(|e| {
            // Note: This map_err is a good candidate for .inspect_err(f) when it becomes stable
            eprintln!(
                "Error: Error collecting boot argument: '{:?}'. Continuing without boot args.",
                e
            );
        })
        .unwrap_or_default();

        let build_info_proxy = fuchsia_component::client::connect_to_protocol::<BuildInfoMarker>()?;
        let resolved_version = Self::resolve_version_from_proxy(&json_config, build_info_proxy)
            .await
            .context("Failed to load version")?;

        Self::resolve_update_config_from_structs(boot_args, json_config, resolved_version)
    }

    async fn resolve_version_from_proxy(
        json_config: &JsonUpdateConfig,
        build_info_proxy: BuildInfoProxy,
    ) -> Result<String, Error> {
        let version = match &json_config.override_version {
            Some(version) => version.clone(),
            None => {
                let build_info =
                    build_info_proxy.get_build_info().await.context("Failed to read build info")?;
                build_info.version.context("No version string provided by build info component")?
            }
        };
        Ok(version)
    }

    /// Resolves update config from provided boot args and json config.
    ///
    /// Note: Version is resolved separately and passed in.
    fn resolve_update_config_from_structs(
        boot_args: BootloaderArgs,
        json_config: JsonUpdateConfig,
        resolved_version: String,
    ) -> Result<RecoveryUpdateConfig, Error> {
        // resolve channel (vbmeta > config)
        let channel = boot_args.ota_channel.unwrap_or(json_config.default_channel);

        // UpdateType is set by the json config
        let update_type: UpdateType = match json_config.update_type {
            JsonUpdateType::Omaha(json_app_id, json_service_url) => {
                UpdateType::Omaha {
                    // Resolve app id (vbmeta > config), config must provide a fallback value
                    app_id: boot_args.omaha_app_id.unwrap_or(json_app_id),
                    // Resolve service url (vbmeta > config > hard-coded)
                    service_url: boot_args
                        .omaha_url
                        .or(json_service_url)
                        .unwrap_or(String::from(DEFAULT_OMAHA_SERVICE_URL)),
                }
            }
            JsonUpdateType::Tuf => UpdateType::Tuf,
        };

        Ok(RecoveryUpdateConfig {
            channel: channel,
            update_type: update_type,
            version: resolved_version,
        })
    }
}

/// Temporary struct to deserialize configs provided with config-data
/// TODO(b/259495731) Switch from deprecated config-data
// pub for integration tests
#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct JsonUpdateConfig {
    pub default_channel: String,
    pub update_type: JsonUpdateType,
    pub override_version: Option<String>,
}

//pub for integration tests
#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum JsonUpdateType {
    /// Designates an Omaha based update
    /// Parameters:
    ///     app_id: The omaha application id
    ///     omaha_service_url: Override the default omaha service to query
    Omaha(String, Option<String>),
    /// Designates a TUF based update
    Tuf,
}

impl JsonUpdateConfig {
    fn load_json_config_data() -> Result<JsonUpdateConfig, Error> {
        let ota_config: JsonUpdateConfig = serde_json::from_reader(BufReader::new(
            File::open(PATH_TO_RECOVERY_CONFIG).context("Failed to find update config data")?,
        ))?;
        Ok(ota_config)
    }
}

/// A holder for values read by the `fuchsia.boot.arguments` protocol.
/// These boot args are expected to be read by the bootloader from the current zircon slot's vbmeta
/// file (usually zircon_r) and passed to boot zircon.
#[derive(Debug, Default, PartialEq)]
struct BootloaderArgs {
    omaha_app_id: Option<String>,
    omaha_url: Option<String>,
    ota_channel: Option<String>,
}

impl BootloaderArgs {
    async fn load_from_proxy(arguments_proxy: ArgumentsProxy) -> Result<BootloaderArgs, Error> {
        let keys = vec!["omaha_app_id", "omaha_url", "ota_channel"];
        let num_keys = keys.len();
        let boot_args: Vec<Option<String>> = arguments_proxy
            .get_strings(&mut keys.into_iter())
            .await
            .context("No boot args available.")?;
        if boot_args.len() != num_keys {
            bail!("Boot args returned {} values, expected {}", boot_args.len(), num_keys);
        }
        Ok(BootloaderArgs {
            omaha_app_id: boot_args[0].clone(),
            omaha_url: boot_args[1].clone(),
            ota_channel: boot_args[2].clone(),
        })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_buildinfo::{BuildInfo, ProviderRequest as BuildInfoRequest},
        fuchsia, fuchsia_async as fasync,
        futures::prelude::*,
        maplit::hashmap,
        mock_boot_arguments::MockBootArgumentsService,
        pretty_assertions::assert_eq,
        std::{collections::HashMap, sync::Arc},
    };

    fn empty_bootloader_args() -> BootloaderArgs {
        BootloaderArgs { omaha_app_id: None, omaha_url: None, ota_channel: None }
    }

    // resolve config from structs tests
    #[test]
    fn resolve_config_expected_in_field_use_case() {
        let boot_args = BootloaderArgs {
            omaha_app_id: Some("avb_app_id".to_string()),
            omaha_url: None,
            ota_channel: Some("avb_channel".to_string()),
        };
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Omaha("json_app_id".to_string(), None),
            override_version: None,
        };

        let resolved_config = RecoveryUpdateConfig::resolve_update_config_from_structs(
            boot_args,
            json_config,
            "resolved_version".to_string(),
        );
        assert_eq!(
            resolved_config.unwrap(),
            RecoveryUpdateConfig {
                channel: "avb_channel".to_string(),
                update_type: UpdateType::Omaha {
                    app_id: "avb_app_id".to_string(),
                    service_url: DEFAULT_OMAHA_SERVICE_URL.to_string(),
                },
                version: "resolved_version".to_string(),
            }
        );
    }

    #[test]
    fn resolve_tuf_config_without_boot_args() {
        let boot_args = empty_bootloader_args();
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Tuf,
            override_version: None,
        };

        let resolved_config = RecoveryUpdateConfig::resolve_update_config_from_structs(
            boot_args,
            json_config,
            "resolved_version".to_string(),
        );
        assert_eq!(
            resolved_config.unwrap(),
            RecoveryUpdateConfig {
                channel: "json_channel".to_string(),
                update_type: UpdateType::Tuf,
                version: "resolved_version".to_string(),
            }
        );
    }

    #[test]
    fn resolve_tuf_config_with_boot_override() {
        let boot_args = BootloaderArgs {
            omaha_app_id: None,
            omaha_url: None,
            ota_channel: Some("avb_channel".to_string()),
        };
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Tuf,
            override_version: Some("json_version".to_string()),
        };

        let resolved_config = RecoveryUpdateConfig::resolve_update_config_from_structs(
            boot_args,
            json_config,
            "resolved_version".to_string(),
        );
        assert_eq!(
            resolved_config.unwrap(),
            RecoveryUpdateConfig {
                channel: "avb_channel".to_string(),
                update_type: UpdateType::Tuf,
                version: "resolved_version".to_string(),
            }
        );
    }

    #[test]
    fn resolve_omaha_config_without_boot_args() {
        let boot_args = empty_bootloader_args();
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Omaha("json_app_id".to_string(), None),
            override_version: None,
        };

        let resolved_config = RecoveryUpdateConfig::resolve_update_config_from_structs(
            boot_args,
            json_config,
            "resolved_version".to_string(),
        );
        assert_eq!(
            resolved_config.unwrap(),
            RecoveryUpdateConfig {
                channel: "json_channel".to_string(),
                update_type: UpdateType::Omaha {
                    app_id: "json_app_id".to_string(),
                    service_url: DEFAULT_OMAHA_SERVICE_URL.to_string(),
                },
                version: "resolved_version".to_string(),
            }
        );
    }

    #[test]
    fn resolve_omaha_json_version_and_url() {
        let boot_args = empty_bootloader_args();
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Omaha(
                "json_app_id".to_string(),
                Some("json_url".to_string()),
            ),
            override_version: Some("json_version".to_string()),
        };

        let resolved_config = RecoveryUpdateConfig::resolve_update_config_from_structs(
            boot_args,
            json_config,
            "resolved_version".to_string(),
        );
        assert_eq!(
            resolved_config.unwrap(),
            RecoveryUpdateConfig {
                channel: "json_channel".to_string(),
                update_type: UpdateType::Omaha {
                    app_id: "json_app_id".to_string(),
                    service_url: "json_url".to_string(),
                },
                version: "resolved_version".to_string(),
            }
        );
    }

    #[test]
    fn resolve_omaha_all_boot_args() {
        let boot_args = BootloaderArgs {
            omaha_app_id: Some("avb_app_id".to_string()),
            omaha_url: Some("avb_url".to_string()),
            ota_channel: Some("avb_channel".to_string()),
        };
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Omaha("json_app_id".to_string(), None),
            override_version: None,
        };

        let resolved_config = RecoveryUpdateConfig::resolve_update_config_from_structs(
            boot_args,
            json_config,
            "resolved_version".to_string(),
        );
        assert_eq!(
            resolved_config.unwrap(),
            RecoveryUpdateConfig {
                channel: "avb_channel".to_string(),
                update_type: UpdateType::Omaha {
                    app_id: "avb_app_id".to_string(),
                    service_url: "avb_url".to_string(),
                },
                version: "resolved_version".to_string(),
            }
        );
    }

    #[test]
    fn resolve_omaha_all_boot_args_with_json_url() {
        let boot_args = BootloaderArgs {
            omaha_app_id: Some("avb_app_id".to_string()),
            omaha_url: Some("avb_url".to_string()),
            ota_channel: Some("avb_channel".to_string()),
        };
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Omaha(
                "json_app_id".to_string(),
                Some("json_url".to_string()),
            ),
            override_version: None,
        };

        let resolved_config = RecoveryUpdateConfig::resolve_update_config_from_structs(
            boot_args,
            json_config,
            "resolved_version".to_string(),
        );
        assert_eq!(
            resolved_config.unwrap(),
            RecoveryUpdateConfig {
                channel: "avb_channel".to_string(),
                update_type: UpdateType::Omaha {
                    app_id: "avb_app_id".to_string(),
                    service_url: "avb_url".to_string(),
                },
                version: "resolved_version".to_string(),
            }
        );
    }

    // Boot Args proxy tests
    async fn spawn_boot_arg_server_with_values(
        args: HashMap<String, Option<String>>,
    ) -> ArgumentsProxy {
        let mock = Arc::new(MockBootArgumentsService::new(args));
        let (proxy, stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        fasync::Task::spawn(mock.handle_request_stream(stream)).detach();
        proxy
    }

    #[fuchsia::test]
    async fn full_boot_args_from_proxy() {
        let proxy = spawn_boot_arg_server_with_values(hashmap! {
            "omaha_app_id".to_string() => Some("avb_app_id".to_string()),
            "ota_channel".to_string() => Some("avb_channel".to_string()),
            "omaha_url".to_string() => Some("avb_url".to_string()),
        })
        .await;

        let args = BootloaderArgs::load_from_proxy(proxy).await.unwrap();

        assert_eq!(
            args,
            BootloaderArgs {
                omaha_app_id: Some("avb_app_id".to_string()),
                omaha_url: Some("avb_url".to_string()),
                ota_channel: Some("avb_channel".to_string()),
            }
        );
    }

    #[fuchsia::test]
    async fn partial_boot_args_from_proxy() {
        let proxy = spawn_boot_arg_server_with_values(hashmap! {
            "omaha_url".to_string() => Some("avb_url".to_string())
        })
        .await;
        let args = BootloaderArgs::load_from_proxy(proxy).await.unwrap();

        assert_eq!(
            args,
            BootloaderArgs {
                omaha_app_id: None,
                omaha_url: Some("avb_url".to_string()),
                ota_channel: None,
            }
        );
    }

    #[fuchsia::test]
    async fn empty_boot_args_from_proxy() {
        let proxy = spawn_boot_arg_server_with_values(hashmap! {}).await;
        let args = BootloaderArgs::load_from_proxy(proxy).await.unwrap();

        assert_eq!(args, BootloaderArgs { omaha_app_id: None, omaha_url: None, ota_channel: None });
    }

    // Resolve version from proxy tests
    #[fuchsia::test]
    async fn resolve_version_from_proxy() {
        let (proxy, mut stream) = create_proxy_and_stream::<BuildInfoMarker>().unwrap();
        fasync::Task::local(async move {
            match stream.next().await.unwrap() {
                Ok(BuildInfoRequest::GetBuildInfo { responder }) => {
                    responder
                        .send(BuildInfo {
                            version: Some("proxy_version".to_string()),
                            ..BuildInfo::EMPTY
                        })
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .detach();

        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Tuf,
            override_version: None,
        };
        let version =
            RecoveryUpdateConfig::resolve_version_from_proxy(&json_config, proxy).await.unwrap();

        assert_eq!(version, "proxy_version".to_string());
    }

    #[fuchsia::test]
    async fn resolve_version_from_json_override() {
        let (build_info_proxy, mut stream) = create_proxy_and_stream::<BuildInfoMarker>().unwrap();
        fasync::Task::local(async move {
            let _ = stream.next().await.unwrap();
            panic!("Runtime version should not be queried if override provided");
        })
        .detach();
        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Tuf,
            override_version: Some("json_version".to_string()),
        };
        let version =
            RecoveryUpdateConfig::resolve_version_from_proxy(&json_config, build_info_proxy)
                .await
                .unwrap();

        assert_eq!(version, "json_version".to_string());
    }

    #[fuchsia::test]
    async fn error_when_no_version_available() {
        let (build_info_proxy, mut stream) = create_proxy_and_stream::<BuildInfoMarker>().unwrap();
        fasync::Task::local(async move {
            match stream.next().await.unwrap() {
                Ok(BuildInfoRequest::GetBuildInfo { responder }) => {
                    responder.send(BuildInfo::EMPTY).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .detach();

        let json_config = JsonUpdateConfig {
            default_channel: "json_channel".to_string(),
            update_type: JsonUpdateType::Tuf,
            override_version: None,
        };

        let version_result =
            RecoveryUpdateConfig::resolve_version_from_proxy(&json_config, build_info_proxy).await;
        assert_matches!(version_result, Err(_));
    }

    // Serde struct tests
    #[test]
    fn test_no_json_channel() {
        let string_version = r#"
        {
            "override_version": "1.2.3.4",
            "update_type": "tuf"
        }"#;
        let res: Result<JsonUpdateConfig, serde_json::Error> = serde_json::from_str(string_version);
        assert_matches!(res, Err(_));
    }

    #[test]
    fn test_omaha_config_new_url() {
        let a = JsonUpdateConfig {
            default_channel: "some_channel".to_string(),
            override_version: None,
            update_type: JsonUpdateType::Omaha(
                "app_id_here".to_string(),
                Some("https://override.google.com".to_string()),
            ),
        };
        let string_version = r#"{
            "default_channel": "some_channel",
            "update_type": {
                "omaha": [
                    "app_id_here",
                    "https://override.google.com"
                ]
            }
        }"#;
        assert_eq!(a, serde_json::from_str(string_version).unwrap());
    }

    #[test]
    fn test_omaha_config() {
        let a = JsonUpdateConfig {
            default_channel: "channel_from_json".to_string(),
            override_version: None,
            update_type: JsonUpdateType::Omaha("app_id_here".to_string(), None),
        };
        let string_version = r#"{
            "default_channel": "channel_from_json",
            "update_type": {
                "omaha": [
                    "app_id_here", null
                ]
            }
        }"#;
        assert_eq!(a, serde_json::from_str(string_version).unwrap());
    }

    #[test]
    fn test_tuf_config() {
        let a = JsonUpdateConfig {
            default_channel: "channel_from_json".to_string(),
            override_version: Some("1.2.3.4".to_string()),
            update_type: JsonUpdateType::Tuf,
        };
        let string_version = r#"
        {
            "default_channel": "channel_from_json",
            "override_version": "1.2.3.4",
            "update_type": "tuf"
        }"#;
        assert_eq!(a, serde_json::from_str(string_version).unwrap());
    }
}
