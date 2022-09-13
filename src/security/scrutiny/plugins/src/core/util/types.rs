// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Structs used in parsing packages

use {
    crate::core::util::jsons::*,
    cm_fidl_analyzer::{match_absolute_pkg_urls, PkgUrlMatch},
    fuchsia_merkle::Hash,
    fuchsia_url::{AbsoluteComponentUrl, AbsolutePackageUrl},
    once_cell::sync::Lazy,
    serde::Serialize,
    std::{
        collections::{HashMap, HashSet},
        path::PathBuf,
    },
    tracing::warn,
    url::Url,
};

pub static INFERRED_URL_SCHEME: &str = "fuchsia-inferred";
pub static INFERRED_URL: Lazy<Url> =
    Lazy::<Url>::new(|| Url::parse(&format!("{}://", INFERRED_URL_SCHEME)).unwrap());

pub type Protocol = String;
pub type ServiceMapping = HashMap<Protocol, Url>;

pub struct SysManagerConfig {
    pub services: ServiceMapping,
    pub apps: HashSet<AbsoluteComponentUrl>,
}

/// Package- and component-related data extracted from a package identified by a
/// fully-qualified fuchsia package URL.
#[cfg_attr(test, derive(Clone))]
pub struct PackageDefinition {
    /// The URL from which the definition was extracted.
    pub url: AbsolutePackageUrl,
    /// A mapping from internal package paths to merkle root hashes of content
    /// (that is non-meta) files designated in the package meta.far.
    pub contents: HashMap<PathBuf, Hash>,
    /// A mapping from internal package meta paths to meta file contents.
    pub meta: HashMap<PathBuf, Vec<u8>>,
    /// A mapping from internal package paths to component manifest data.
    pub cms: HashMap<PathBuf, ComponentManifest>,
    /// A mapping from internal package paths to config value files.
    pub cvfs: HashMap<String, Vec<u8>>,
}

impl PackageDefinition {
    pub fn new(url: AbsolutePackageUrl, partial: PartialPackageDefinition) -> Self {
        Self {
            url,
            contents: partial.contents,
            meta: partial.meta,
            cms: partial.cms,
            cvfs: partial.cvfs,
        }
    }

    pub fn matches_url(&self, url: &AbsolutePackageUrl) -> bool {
        let url_match = match_absolute_pkg_urls(&self.url, url);
        if url_match == PkgUrlMatch::WeakMatch {
            warn!(
                PkgDefinition.url = %self.url,
                other_url = %url,
                "Lossy match of absolute package URLs",
            );
        }
        url_match != PkgUrlMatch::NoMatch
    }
}

/// Package- and component-related data extracted from an package.
#[derive(Default)]
#[cfg_attr(test, derive(Clone))]
pub struct PartialPackageDefinition {
    /// A mapping from internal package paths to merkle root hashes of content
    /// (that is non-meta) files designated in the package meta.far.
    pub contents: HashMap<PathBuf, Hash>,
    /// A mapping from internal package meta paths to meta file contents.
    pub meta: HashMap<PathBuf, Vec<u8>>,
    /// A mapping from internal package paths to component manifest data.
    pub cms: HashMap<PathBuf, ComponentManifest>,
    /// A mapping from internal package paths to config value files.
    pub cvfs: HashMap<String, Vec<u8>>,
}

#[allow(dead_code)]
#[cfg_attr(test, derive(Clone))]
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

#[derive(Serialize, Clone)]
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
