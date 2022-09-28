// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{warnings::Warning, BUILD_INFO_PROTOCOL, SYSTEM_TEST_SHARD};
use anyhow::{bail, Error};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
pub enum CmxFeature {
    #[serde(rename = "isolated-persistent-storage")]
    IsolatedPersistentStorage,
    #[serde(rename = "isolated-cache-storage")]
    IsolatedCacheStorage,
    #[serde(rename = "isolated-temp")]
    IsolatedTemp,

    #[serde(rename = "factory-data")]
    FactoryData,
    #[serde(rename = "durable-data")]
    DurableData,
    #[serde(rename = "shell-commands")]
    ShellCommands,
    #[serde(rename = "root-ssl-certificates")]
    RootSslCerts,

    #[serde(rename = "config-data")]
    ConfigData,

    #[serde(rename = "dev")]
    Dev,

    #[serde(rename = "hub")]
    Hub,

    #[serde(rename = "build-info")]
    BuildInfo,

    #[serde(rename = "vulkan")]
    Vulkan,

    #[serde(rename = "deprecated-ambient-replace-as-executable")]
    AmbientReplaceAsExecutable,

    #[serde(rename = "deprecated-shell")]
    Shell,

    #[serde(rename = "deprecated-global-dev")]
    GlobalDev,

    #[serde(rename = "deprecated-misc-storage")]
    MiscStorage,
}

impl CmxFeature {
    /// Returns any use decl, warnings, or shards needed to convert the feature to v2.
    pub fn uses(
        &self,
        is_test: bool,
    ) -> Result<(Option<cml::Use>, Option<Warning>, Option<String>), Error> {
        Ok(match self {
            CmxFeature::IsolatedPersistentStorage => (
                Some(cml::Use {
                    storage: Some(cml::Name::try_new("data").unwrap()),
                    path: Some(cml::Path::new("/data").unwrap()),
                    ..Default::default()
                }),
                if !is_test { Some(Warning::StorageIndex) } else { None },
                None,
            ),
            CmxFeature::IsolatedCacheStorage => (
                Some(cml::Use {
                    storage: Some(cml::Name::try_new("cache").unwrap()),
                    path: Some(cml::Path::new("/cache").unwrap()),
                    ..Default::default()
                }),
                None,
                None,
            ),
            CmxFeature::IsolatedTemp => (
                Some(cml::Use {
                    storage: Some(cml::Name::try_new("tmp").unwrap()),
                    path: Some(cml::Path::new("/tmp").unwrap()),
                    ..Default::default()
                }),
                None,
                None,
            ),
            CmxFeature::FactoryData => (
                Some(cml::Use {
                    directory: Some(cml::Name::try_new("factory").unwrap()),
                    rights: Some(cml::Rights(vec![cml::Right::ReadAlias])),
                    path: Some(cml::Path::new("/factory").unwrap()),
                    ..Default::default()
                }),
                None,
                None,
            ),
            CmxFeature::DurableData => (
                Some(cml::Use {
                    directory: Some(cml::Name::try_new("durable").unwrap()),
                    rights: Some(cml::Rights(vec![cml::Right::ReadAlias])),
                    path: Some(cml::Path::new("/durable").unwrap()),
                    ..Default::default()
                }),
                None,
                None,
            ),
            CmxFeature::ShellCommands => (
                Some(cml::Use {
                    directory: Some(cml::Name::try_new("bin").unwrap()),
                    rights: Some(cml::Rights(vec![cml::Right::ReadAlias])),
                    path: Some(cml::Path::new("/bin").unwrap()),
                    ..Default::default()
                }),
                None,
                None,
            ),
            CmxFeature::RootSslCerts => (
                Some(cml::Use {
                    directory: Some(cml::Name::try_new("root-ssl-certificates").unwrap()),
                    rights: Some(cml::Rights(vec![cml::Right::ReadAlias])),
                    path: Some(cml::Path::new("/config/ssl").unwrap()),
                    ..Default::default()
                }),
                None,
                if is_test { Some(SYSTEM_TEST_SHARD.to_string()) } else { None },
            ),
            CmxFeature::ConfigData => (
                Some(cml::Use {
                    directory: Some(cml::Name::try_new("config-data").unwrap()),
                    rights: Some(cml::Rights(vec![cml::Right::ReadAlias])),
                    path: Some(cml::Path::new("/config/data").unwrap()),
                    ..Default::default()
                }),
                if is_test { Some(Warning::ConfigDataInTest) } else { None },
                None,
            ),
            CmxFeature::BuildInfo => (
                Some(cml::Use {
                    protocol: Some(cml::OneOrMany::One(
                        cml::Name::try_new(BUILD_INFO_PROTOCOL).unwrap(),
                    )),
                    ..Default::default()
                }),
                Some(Warning::BuildInfoImpl),
                None,
            ),

            // correctly replacing the hub with event streams does require `use`s but we can't
            // statically know which events to request here, just let the user fill it in from docs
            CmxFeature::Hub => (None, Some(Warning::UsesHub), None),

            // sandbox requests actual paths, no uses needed here but does need system tests
            CmxFeature::Dev => {
                (None, None, if is_test { Some(SYSTEM_TEST_SHARD.to_string()) } else { None })
            }

            // unsupported features:
            CmxFeature::Vulkan => {
                bail!("vulkan feature is unsupported for conversion")
            }
            CmxFeature::AmbientReplaceAsExecutable => {
                bail!("deprecated-ambient-replace-as-executable feature is unsupported for conversion")
            }
            CmxFeature::Shell => {
                bail!("deprecated-shell feature is unsupported for conversion")
            }
            CmxFeature::GlobalDev => {
                bail!("deprecated-global-dev feature is unsupported for conversion")
            }
            CmxFeature::MiscStorage => {
                bail!("deprecated-misc-storage feature is unsupported for conversion")
            }
        })
    }
}
