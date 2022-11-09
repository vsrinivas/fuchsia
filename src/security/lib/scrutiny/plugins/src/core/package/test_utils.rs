// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        package::reader::{read_service_package_definition, PackageReader},
        util::{
            jsons::ServicePackageDefinition,
            types::{
                ComponentManifest, ComponentV1Manifest, PackageDefinition, PartialPackageDefinition,
            },
        },
    },
    anyhow::{anyhow, Result},
    fuchsia_merkle::{Hash, HASH_SIZE},
    fuchsia_url::{AbsolutePackageUrl, PackageName, PackageVariant},
    scrutiny::model::model::DataModel,
    scrutiny_testing::{artifact::AppendResult, fake::fake_model_config, TEST_REPO_URL},
    std::{
        collections::{HashMap, HashSet},
        path::PathBuf,
        sync::Arc,
    },
};

pub struct MockPackageReader {
    pkg_urls: Option<Vec<AbsolutePackageUrl>>,
    update_pkg_def: Option<PartialPackageDefinition>,
    pkg_defs: HashMap<AbsolutePackageUrl, PackageDefinition>,
    deps: HashSet<PathBuf>,
}

impl MockPackageReader {
    pub fn new() -> Self {
        Self {
            pkg_urls: None,
            update_pkg_def: None,
            pkg_defs: HashMap::new(),
            deps: HashSet::new(),
        }
    }

    pub fn append_update_package(
        &mut self,
        pkg_urls: Vec<AbsolutePackageUrl>,
        update_pkg_def: PartialPackageDefinition,
    ) -> AppendResult {
        let result = if self.pkg_urls.is_some() || self.update_pkg_def.is_some() {
            AppendResult::Merged
        } else {
            AppendResult::Appended
        };
        self.pkg_urls = Some(pkg_urls);
        self.update_pkg_def = Some(update_pkg_def);
        result
    }

    pub fn append_pkg_def(&mut self, pkg_def: PackageDefinition) -> AppendResult {
        if let Some(existing_pkg_def) = self.pkg_defs.get_mut(&pkg_def.url) {
            *existing_pkg_def = pkg_def;
            AppendResult::Merged
        } else {
            self.pkg_defs.insert(pkg_def.url.clone(), pkg_def);
            AppendResult::Appended
        }
    }

    pub fn append_dep(&mut self, path_buf: PathBuf) -> AppendResult {
        if self.deps.insert(path_buf) {
            AppendResult::Appended
        } else {
            AppendResult::Merged
        }
    }
}

impl PackageReader for MockPackageReader {
    fn read_package_urls(&mut self) -> Result<Vec<AbsolutePackageUrl>> {
        self.pkg_urls.as_ref().map(|pkg_urls| pkg_urls.clone()).ok_or(anyhow!(
            "Attempt to read package URLs from mock package reader with no package URLs set"
        ))
    }

    fn read_package_definition(
        &mut self,
        pkg_url: &AbsolutePackageUrl,
    ) -> Result<PackageDefinition> {
        self.pkg_defs.get(pkg_url).map(|pkg_def| pkg_def.clone()).ok_or_else(|| {
            anyhow!("Mock package reader contains no package definition for {:?}", pkg_url)
        })
    }

    fn read_update_package_definition(&mut self) -> Result<PartialPackageDefinition> {
        self.update_pkg_def.as_ref().map(|update_pkg_def| update_pkg_def.clone()).ok_or(anyhow!(
            "Attempt to read update package from mock package reader with no update package set"
        ))
    }

    fn read_service_package_definition(&mut self, data: &[u8]) -> Result<ServicePackageDefinition> {
        read_service_package_definition(data)
    }

    fn get_deps(&self) -> HashSet<PathBuf> {
        self.deps.clone()
    }
}

pub fn create_test_sandbox(uses: Vec<String>) -> ComponentV1Manifest {
    ComponentV1Manifest {
        dev: None,
        services: Some(uses),
        system: None,
        pkgfs: None,
        features: None,
    }
}

/// Create component manifest v1 (cmx) entries.
pub fn create_test_cmx_map(
    entries: Vec<(PathBuf, ComponentV1Manifest)>,
) -> HashMap<PathBuf, ComponentManifest> {
    entries.into_iter().map(|entry| (entry.0, ComponentManifest::Version1(entry.1))).collect()
}

/// Create component manifest v2 (cm) entries.
pub fn create_test_cm_map(entries: Vec<(PathBuf, Vec<u8>)>) -> HashMap<PathBuf, ComponentManifest> {
    entries.into_iter().map(|entry| (entry.0, ComponentManifest::Version2(entry.1))).collect()
}

pub fn create_test_partial_package_with_cms(
    cms: HashMap<PathBuf, ComponentManifest>,
) -> PartialPackageDefinition {
    PartialPackageDefinition {
        meta: HashMap::new(),
        contents: HashMap::new(),
        cms,
        cvfs: HashMap::new(),
    }
}

pub fn create_test_package_with_cms(
    name: PackageName,
    variant: Option<PackageVariant>,
    cms: HashMap<PathBuf, ComponentManifest>,
) -> PackageDefinition {
    PackageDefinition {
        url: AbsolutePackageUrl::new(
            TEST_REPO_URL.clone(),
            name,
            variant,
            Some([0; HASH_SIZE].into()),
        ),
        meta: HashMap::new(),
        contents: HashMap::new(),
        cms,
        cvfs: Default::default(),
    }
}

pub fn create_test_partial_package_with_contents(
    contents: HashMap<PathBuf, Hash>,
) -> PartialPackageDefinition {
    PartialPackageDefinition {
        meta: HashMap::new(),
        contents: contents,
        cms: HashMap::new(),
        cvfs: HashMap::new(),
    }
}

pub fn create_test_package_with_contents(
    name: PackageName,
    variant: Option<PackageVariant>,
    contents: HashMap<PathBuf, Hash>,
) -> PackageDefinition {
    PackageDefinition {
        url: AbsolutePackageUrl::new(
            TEST_REPO_URL.clone(),
            name,
            variant,
            Some([0; HASH_SIZE].into()),
        ),
        meta: HashMap::new(),
        contents: contents,
        cms: HashMap::new(),
        cvfs: HashMap::new(),
    }
}

pub fn create_test_package_with_meta(
    name: PackageName,
    variant: Option<PackageVariant>,
    meta: HashMap<PathBuf, Vec<u8>>,
) -> PackageDefinition {
    PackageDefinition {
        url: AbsolutePackageUrl::new(
            TEST_REPO_URL.clone(),
            name,
            variant,
            Some([0; HASH_SIZE].into()),
        ),
        meta,
        contents: HashMap::new(),
        cms: HashMap::new(),
        cvfs: HashMap::new(),
    }
}

pub fn create_svc_pkg_bytes(services: Vec<(String, String)>, apps: Vec<String>) -> Vec<u8> {
    let services: HashMap<String, String> = services.into_iter().collect();
    serde_json::to_vec(&serde_json::json!({
        "services": services,
        "apps": apps,
    }))
    .unwrap()
}

pub fn create_svc_pkg_bytes_with_array(
    services: Vec<(String, Vec<String>)>,
    apps: Vec<String>,
) -> Vec<u8> {
    let services: HashMap<String, Vec<String>> = services.into_iter().collect();
    serde_json::to_vec(&serde_json::json!({
        "services": services,
        "apps": apps,
    }))
    .unwrap()
}

/// Creates a package definition which maps a set of service names to a set
/// of files in the package which represent components that provide that
/// service.
pub fn create_svc_pkg_def(
    services: Vec<(String, String)>,
    apps: Vec<String>,
) -> ServicePackageDefinition {
    ServicePackageDefinition {
        services: Some(
            services.into_iter().map(|entry| (entry.0, serde_json::json!(entry.1))).collect(),
        ),
        apps: Some(apps.clone()),
    }
}

pub fn create_model() -> (String, Arc<DataModel>) {
    let config = fake_model_config();
    let uri_clone = config.uri();
    (uri_clone, Arc::new(DataModel::new(config).unwrap()))
}
