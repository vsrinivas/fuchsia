// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_url::UnpinnedAbsolutePackageUrl,
    omaha_client::cup_ecdsa::PublicKeys,
    serde::{Deserialize, Serialize},
};

#[cfg(target_os = "fuchsia")]
use {
    anyhow::{Context as _, Error},
    fuchsia_zircon as zx,
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
    #[serde(default)]
    pub executable: bool,
    pub public_keys: PublicKeys,
}

#[cfg(target_os = "fuchsia")]
impl EagerPackageConfigs {
    /// Read eager config from namespace. Returns an empty instance of `EagerPackageConfigs` in
    /// case config was not found.
    pub async fn from_namespace() -> Result<Self, Error> {
        match fuchsia_fs::file::read_in_namespace(EAGER_PACKAGE_CONFIG_PATH).await {
            Ok(json) => Ok(serde_json::from_slice(&json).context("parsing eager package config")?),
            Err(e) => match e.into_inner() {
                fuchsia_fs::file::ReadError::Open(fuchsia_fs::node::OpenError::OpenError(
                    status,
                ))
                | fuchsia_fs::file::ReadError::Fidl(fidl::Error::ClientChannelClosed {
                    status,
                    ..
                }) if status == zx::Status::NOT_FOUND => {
                    Ok(EagerPackageConfigs { packages: Vec::new() })
                }
                err => Err(err).with_context(|| {
                    format!("Error reading eager package config file {EAGER_PACKAGE_CONFIG_PATH}")
                }),
            },
        }
    }
}
