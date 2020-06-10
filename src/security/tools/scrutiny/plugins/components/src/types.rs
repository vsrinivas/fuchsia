// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Structs used in parsing packages

use {crate::jsons::*, serde::Serialize, std::collections::HashMap};

pub type ServiceMapping = HashMap<String, String>;

pub struct PackageDefinition {
    pub url: String,
    pub merkle: String,
    pub typ: PackageType,
    pub contents: HashMap<String, String>,
    pub cms: HashMap<String, CmxDefinition>, // TODO: CFv2
}

pub enum PackageType {
    PACKAGE,
    BUILTIN,
}

// TODO: Use a trait to allow for CFv2 support
pub struct CmxDefinition {
    pub sandbox: Option<SandboxDefinition>,
    // The other values in CmxJson don't seem to be used
}

impl From<CmxJson> for CmxDefinition {
    fn from(other: CmxJson) -> Self {
        CmxDefinition {
            sandbox: {
                if let Some(sandbox) = other.sandbox {
                    Some(SandboxDefinition::from(sandbox))
                } else {
                    None
                }
            },
        }
    }
}

impl From<Option<Manifest>> for CmxDefinition {
    fn from(other: Option<Manifest>) -> Self {
        CmxDefinition {
            sandbox: {
                if let Some(manifest) = other {
                    Some(SandboxDefinition::from(manifest.sandbox))
                } else {
                    None
                }
            },
        }
    }
}

#[derive(Serialize)]
pub struct SandboxDefinition {
    pub dev: Option<Vec<String>>,
    pub services: Option<Vec<String>>,
    pub system: Option<Vec<String>>,
    pub pkgfs: Option<Vec<String>>,
    pub features: Option<Vec<String>>,
}

impl From<Sandbox> for SandboxDefinition {
    fn from(other: Sandbox) -> Self {
        SandboxDefinition {
            dev: other.dev,
            services: other.services,
            system: other.system,
            pkgfs: other.pkgfs,
            features: other.features,
        }
    }
}
