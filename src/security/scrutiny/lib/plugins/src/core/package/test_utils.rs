// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        package::getter::PackageGetter,
        package::reader::PackageReader,
        util::{
            jsons::{ServicePackageDefinition, TargetsJson},
            types::{ComponentManifest, ComponentV1Manifest, PackageDefinition},
        },
    },
    anyhow::{anyhow, Result},
    scrutiny::model::model::DataModel,
    scrutiny_testing::fake::fake_model_config,
    std::io::{Error, ErrorKind},
    std::{
        collections::{HashMap, HashSet},
        sync::{Arc, RwLock},
    },
};

pub struct MockPackageGetter {
    bytes: RwLock<Vec<Vec<u8>>>,
    deps: HashSet<String>,
}

impl MockPackageGetter {
    pub fn new() -> Self {
        Self { bytes: RwLock::new(Vec::new()), deps: HashSet::new() }
    }

    pub fn append_bytes(&self, byte_vec: Vec<u8>) {
        self.bytes.write().unwrap().push(byte_vec);
    }
}

impl PackageGetter for MockPackageGetter {
    fn read_raw(&mut self, path: &str) -> std::io::Result<Vec<u8>> {
        let mut borrow = self.bytes.write().unwrap();
        {
            if borrow.len() == 0 {
                return Err(Error::new(
                    ErrorKind::Other,
                    "No more byte vectors left to return. Maybe append more?",
                ));
            }
            self.deps.insert(path.to_string());
            Ok(borrow.remove(0))
        }
    }

    fn get_deps(&self) -> HashSet<String> {
        self.deps.clone()
    }
}

pub struct MockPackageReader {
    targets: RwLock<Vec<TargetsJson>>,
    package_defs: RwLock<Vec<PackageDefinition>>,
    service_package_defs: RwLock<Vec<ServicePackageDefinition>>,
    deps: HashSet<String>,
}

impl MockPackageReader {
    pub fn new() -> Self {
        Self {
            targets: RwLock::new(Vec::new()),
            package_defs: RwLock::new(Vec::new()),
            service_package_defs: RwLock::new(Vec::new()),
            deps: HashSet::new(),
        }
    }

    // Adds values to the FIFO queue for targets
    pub fn append_target(&self, target: TargetsJson) {
        self.targets.write().unwrap().push(target);
    }
    // Adds values to the FIFO queue for package_defs
    pub fn append_pkg_def(&self, package_def: PackageDefinition) {
        self.package_defs.write().unwrap().push(package_def);
    }
    // Adds values to the FIFO queue for service_package_defs
    pub fn append_service_pkg_def(&self, svc_pkg_def: ServicePackageDefinition) {
        self.service_package_defs.write().unwrap().push(svc_pkg_def);
    }
}

impl PackageReader for MockPackageReader {
    fn read_targets(&mut self) -> Result<TargetsJson> {
        let mut borrow = self.targets.write().unwrap();
        {
            if borrow.len() == 0 {
                return Err(anyhow!("No more targets left to return. Maybe append more?"));
            }
            self.deps.insert("targets.json".to_string());
            Ok(borrow.remove(0))
        }
    }

    fn read_package_definition(
        &mut self,
        _pkg_name: &str,
        _merkle: &str,
    ) -> Result<PackageDefinition> {
        let mut borrow = self.package_defs.write().unwrap();
        {
            if borrow.len() == 0 {
                return Err(anyhow!("No more package_defs left to return. Maybe append more?"));
            }
            self.deps.insert(borrow[0].merkle.clone());
            Ok(borrow.remove(0))
        }
    }

    fn read_service_package_definition(
        &mut self,
        _data: String,
    ) -> Result<ServicePackageDefinition> {
        let mut borrow = self.service_package_defs.write().unwrap();
        {
            if borrow.len() == 0 {
                return Err(anyhow!(
                    "No more service_package_defs left to return. Maybe append more?"
                ));
            }
            Ok(borrow.remove(0))
        }
    }

    fn get_deps(&self) -> HashSet<String> {
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
    entries: Vec<(String, ComponentV1Manifest)>,
) -> HashMap<String, ComponentManifest> {
    entries.into_iter().map(|entry| (entry.0, ComponentManifest::Version1(entry.1))).collect()
}

/// Create component manifest v2 (cm) entries.
pub fn create_test_cm_map(entries: Vec<(String, Vec<u8>)>) -> HashMap<String, ComponentManifest> {
    entries.into_iter().map(|entry| (entry.0, ComponentManifest::Version2(entry.1))).collect()
}

pub fn create_test_package_with_cms(
    url: String,
    cms: HashMap<String, ComponentManifest>,
) -> PackageDefinition {
    PackageDefinition {
        url: url,
        merkle: String::from("0"),
        meta: HashMap::new(),
        contents: HashMap::new(),
        cms: cms,
    }
}

pub fn create_test_package_with_contents(
    url: String,
    contents: HashMap<String, String>,
) -> PackageDefinition {
    PackageDefinition {
        url: url,
        merkle: String::from("0"),
        meta: HashMap::new(),
        contents: contents,
        cms: HashMap::new(),
    }
}

pub fn create_test_package_with_meta(
    url: String,
    meta: HashMap<String, String>,
) -> PackageDefinition {
    PackageDefinition {
        url: url,
        merkle: String::from("0"),
        meta,
        contents: HashMap::new(),
        cms: HashMap::new(),
    }
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

pub fn create_svc_pkg_def_with_array(
    services: Vec<(String, Vec<String>)>,
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
    (uri_clone, Arc::new(DataModel::connect(config).unwrap()))
}
