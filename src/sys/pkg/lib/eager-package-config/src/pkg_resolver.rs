// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_url::UnpinnedAbsolutePackageUrl,
    omaha_client::cup_ecdsa::PublicKeys,
    serde::{Deserialize, Serialize},
    version::Version,
};

#[cfg(target_os = "fuchsia")]
use {
    fidl_fuchsia_io as fio, fuchsia_zircon as zx, futures::stream::StreamExt as _,
    std::collections::HashSet,
};

#[cfg(target_os = "fuchsia")]
const EAGER_PACKAGE_CONFIG_PATH: &str = "/config/data/eager_package_config.json";

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct EagerPackageConfigs {
    pub packages: Vec<EagerPackageConfig>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct EagerPackageConfig {
    pub url: UnpinnedAbsolutePackageUrl,
    #[serde(default, skip_serializing_if = "is_false")]
    pub executable: bool,
    pub public_keys: PublicKeys,
    pub minimum_required_version: Version,
    #[serde(default = "return_true", skip_serializing_if = "bool::clone")]
    pub cache_fallback: bool,
}

#[cfg(target_os = "fuchsia")]
impl EagerPackageConfigs {
    /// Read eager config from namespace. Returns an empty instance of `EagerPackageConfigs` in
    /// case config was not found.
    pub async fn from_namespace() -> Result<Self, EagerPackageConfigsError> {
        Self::from_path(EAGER_PACKAGE_CONFIG_PATH).await
    }

    async fn from_path(path: &str) -> Result<Self, EagerPackageConfigsError> {
        let proxy = fuchsia_fs::file::open_in_namespace(
            path,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
        )?;
        match fuchsia_fs::file::read(&proxy).await {
            Ok(json) => Self::from_json(&json),
            Err(e) => {
                if matches!(
                    proxy.take_event_stream().next().await,
                    Some(Ok(fio::FileEvent::OnOpen_{s, ..}))
                        if s == zx::Status::NOT_FOUND.into_raw()
                ) {
                    Ok(EagerPackageConfigs { packages: Vec::new() })
                } else {
                    Err(EagerPackageConfigsError::Read(e))
                }
            }
        }
    }

    fn from_json(json: &[u8]) -> Result<Self, EagerPackageConfigsError> {
        let configs: Self = serde_json::from_slice(json)?;
        if configs.packages.iter().map(|config| config.url.path()).collect::<HashSet<_>>().len()
            < configs.packages.len()
        {
            return Err(EagerPackageConfigsError::DuplicatePath);
        }
        Ok(configs)
    }
}

#[cfg(target_os = "fuchsia")]
#[derive(Debug, thiserror::Error)]
pub enum EagerPackageConfigsError {
    #[error("open eager package config")]
    Open(#[from] fuchsia_fs::node::OpenError),
    #[error("read eager package config")]
    Read(#[from] fuchsia_fs::file::ReadError),
    #[error("parse eager package config from json")]
    Json(#[from] serde_json::Error),
    #[error("eager package URL must have unique path")]
    DuplicatePath,
}

fn return_true() -> bool {
    true
}

fn is_false(b: &bool) -> bool {
    !b
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        omaha_client::cup_ecdsa::test_support::{
            make_default_json_public_keys_for_test, make_default_public_keys_for_test,
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn not_found_tmp() {
        let configs = EagerPackageConfigs::from_path("/tmp/not-found").await.unwrap();
        assert_eq!(configs.packages, vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn not_found_pkg() {
        let configs = EagerPackageConfigs::from_path("/pkg/not-found").await.unwrap();
        assert_eq!(configs.packages, vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn success() {
        let json = serde_json::json!({
            "packages":[
                {
                    "url": "fuchsia-pkg://example.com/package_service_1",
                    "public_keys": make_default_json_public_keys_for_test(),
                    "minimum_required_version": "1.2.3.4"
                }
            ]
        });
        assert_eq!(
            EagerPackageConfigs::from_json(json.to_string().as_bytes()).unwrap(),
            EagerPackageConfigs {
                packages: vec![EagerPackageConfig {
                    url: "fuchsia-pkg://example.com/package_service_1".parse().unwrap(),
                    executable: false,
                    public_keys: make_default_public_keys_for_test(),
                    minimum_required_version: [1, 2, 3, 4].into(),
                    cache_fallback: true,
                }]
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn duplicate_path() {
        let json = serde_json::json!({
            "packages":[
                {
                    "url": "fuchsia-pkg://example.com/package_service_1",
                    "public_keys": make_default_json_public_keys_for_test(),
                    "minimum_required_version": "1.2.3.4"
                },
                {
                    "url": "fuchsia-pkg://another-example.com/package_service_1",
                    "public_keys": make_default_json_public_keys_for_test(),
                    "minimum_required_version": "1.2.3.4"
                }
            ]
        });
        assert_matches!(
            EagerPackageConfigs::from_json(json.to_string().as_bytes()),
            Err(EagerPackageConfigsError::DuplicatePath)
        );
    }
}
