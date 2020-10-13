// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Structs used in parsing packages

use {crate::core::jsons::*, serde::Serialize, std::collections::HashMap};

pub type ServiceMapping = HashMap<String, String>;

///  Packages can either be builtin (infered) or from a FAR package.
pub enum PackageType {
    Package,
    Builtin,
}

/// Defines the interior properties of a package that we care about for
/// constructing the model.
pub struct PackageDefinition {
    pub url: String,
    pub merkle: String,
    pub typ: PackageType,
    pub contents: HashMap<String, String>,
    pub cms: HashMap<String, ComponentManifest>,
}

#[allow(dead_code)]
pub enum ComponentManifest {
    Empty,
    Version1(ComponentV1Manifest),
    Version2(Vec<u8>),
}

impl From<Vec<u8>> for ComponentManifest {
    fn from(other: Vec<u8>) -> Self {
        ComponentManifest::Version2(other)
    }
}

impl From<CmxJson> for ComponentManifest {
    fn from(other: CmxJson) -> Self {
        if let Some(sandbox) = other.sandbox {
            ComponentManifest::Version1(ComponentV1Manifest::from(sandbox))
        } else {
            ComponentManifest::Empty
        }
    }
}

impl From<Option<Manifest>> for ComponentManifest {
    fn from(other: Option<Manifest>) -> Self {
        if let Some(manifest) = other {
            ComponentManifest::Version1(ComponentV1Manifest::from(manifest.sandbox))
        } else {
            ComponentManifest::Empty
        }
    }
}

#[derive(Serialize)]
pub struct ComponentV1Manifest {
    pub dev: Option<Vec<String>>,
    pub services: Option<Vec<String>>,
    pub system: Option<Vec<String>>,
    pub pkgfs: Option<Vec<String>>,
    pub features: Option<Vec<String>>,
}

impl From<Sandbox> for ComponentV1Manifest {
    fn from(other: Sandbox) -> Self {
        ComponentV1Manifest {
            dev: other.dev,
            services: other.services,
            system: other.system,
            pkgfs: other.pkgfs,
            features: other.features,
        }
    }
}
