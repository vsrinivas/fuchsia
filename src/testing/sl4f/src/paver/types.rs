// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_paver as paver;
use serde::{Deserialize, Serialize};

/// Enum for supported paver related commands.
#[derive(Debug)]
pub(super) enum Method {
    QueryActiveConfiguration,
    QueryCurrentConfiguration,
    QueryConfigurationStatus,
    ReadAsset,
}

impl std::str::FromStr for Method {
    type Err = Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "QueryActiveConfiguration" => Ok(Method::QueryActiveConfiguration),
            "QueryCurrentConfiguration" => Ok(Method::QueryCurrentConfiguration),
            "QueryConfigurationStatus" => Ok(Method::QueryConfigurationStatus),
            "ReadAsset" => Ok(Method::ReadAsset),
            _ => return Err(format_err!("Invalid paver facade method: {}", method)),
        }
    }
}

/// Identifies a particular boot slot.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq, Clone)]
#[serde(rename_all = "snake_case")]
pub(super) enum Configuration {
    A,
    B,
    Recovery,
}

impl From<paver::Configuration> for Configuration {
    fn from(x: paver::Configuration) -> Self {
        match x {
            paver::Configuration::A => Configuration::A,
            paver::Configuration::B => Configuration::B,
            paver::Configuration::Recovery => Configuration::Recovery,
        }
    }
}

impl From<Configuration> for paver::Configuration {
    fn from(x: Configuration) -> Self {
        match x {
            Configuration::A => paver::Configuration::A,
            Configuration::B => paver::Configuration::B,
            Configuration::Recovery => paver::Configuration::Recovery,
        }
    }
}

/// Identifies an image within a [Configuration].
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq, Clone)]
#[serde(rename_all = "snake_case")]
pub(super) enum Asset {
    Kernel,
    VerifiedBootMetadata,
}

impl From<paver::Asset> for Asset {
    fn from(x: paver::Asset) -> Self {
        match x {
            paver::Asset::Kernel => Asset::Kernel,
            paver::Asset::VerifiedBootMetadata => Asset::VerifiedBootMetadata,
        }
    }
}

impl From<Asset> for paver::Asset {
    fn from(x: Asset) -> Self {
        match x {
            Asset::Kernel => paver::Asset::Kernel,
            Asset::VerifiedBootMetadata => paver::Asset::VerifiedBootMetadata,
        }
    }
}

/// The bootable status of a particular [Configuration].
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub(super) enum ConfigurationStatus {
    Healthy,
    Pending,
    Unbootable,
}

impl From<paver::ConfigurationStatus> for ConfigurationStatus {
    fn from(x: paver::ConfigurationStatus) -> Self {
        match x {
            paver::ConfigurationStatus::Healthy => ConfigurationStatus::Healthy,
            paver::ConfigurationStatus::Pending => ConfigurationStatus::Pending,
            paver::ConfigurationStatus::Unbootable => ConfigurationStatus::Unbootable,
        }
    }
}

impl From<ConfigurationStatus> for paver::ConfigurationStatus {
    fn from(x: ConfigurationStatus) -> Self {
        match x {
            ConfigurationStatus::Healthy => paver::ConfigurationStatus::Healthy,
            ConfigurationStatus::Pending => paver::ConfigurationStatus::Pending,
            ConfigurationStatus::Unbootable => paver::ConfigurationStatus::Unbootable,
        }
    }
}
