// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        collection::{
            Capability, Component, Components, Manifest, ManifestData, Manifests, Package,
            Packages, ProtocolCapability, Route, Routes, Sysmgr, Zbi,
        },
        package::{artifact::ArtifactGetter, getter::PackageGetter, reader::*},
        util::types::*,
    },
    anyhow::{anyhow, Result},
    cm_fidl_validator,
    fidl::encoding::decode_persistent,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::boot_url::BootUrl,
    lazy_static::lazy_static,
    log::{debug, error, info, warn},
    regex::Regex,
    scrutiny::model::{collector::DataCollector, model::DataModel},
    scrutiny_utils::{bootfs::*, zbi::*},
    serde_json::Value,
    std::{
        collections::{HashMap, HashSet},
        str,
        sync::Arc,
    },
};

// Constants/Statics
lazy_static! {
    static ref SERVICE_CONFIG_RE: Regex = Regex::new(r"data/sysmgr/.+\.config").unwrap();
}
pub const CONFIG_DATA_PKG_URL: &str = "fuchsia-pkg://fuchsia.com/config-data";
pub const REPOSITORY_PATH: &str = "amber-files/repository";
pub const UPDATE_PKG_URL: &str = "fuchsia-pkg://fuchsia.com/update";
// The root v2 component manifest.
pub const ROOT_RESOURCE: &str = "meta/root.cm";

/// The PackageDataResponse contains all of the core model information extracted
/// from the Fuchsia Archive (.far) packages from the current build.
pub struct PackageDataResponse {
    pub components: HashMap<String, Component>,
    pub packages: Vec<Package>,
    pub manifests: Vec<Manifest>,
    pub routes: Vec<Route>,
    pub zbi: Option<Zbi>,
}

impl PackageDataResponse {
    pub fn new(
        components: HashMap<String, Component>,
        packages: Vec<Package>,
        manifests: Vec<Manifest>,
        routes: Vec<Route>,
        zbi: Option<Zbi>,
    ) -> Self {
        Self { components, packages, manifests, routes, zbi }
    }
}

/// The PackageDataCollector is a core collector in Scrutiny that is
/// responsible for extracting data from Fuchsia Archives (.far). This collector
/// scans every single package and extracts all of the manifests and files.
/// Using this raw data it constructs all the routes and components in the
/// model.
#[derive(Default)]
pub struct PackageDataCollector {}

impl PackageDataCollector {
    /// Retrieves the set of packages from the current target build returning
    /// them sorted by package url.
    fn get_packages(
        &self,
        package_reader: &Box<dyn PackageReader>,
    ) -> Result<Vec<PackageDefinition>> {
        // Retrieve the JSON packages definition from the package server.
        let targets = package_reader.read_targets()?;
        let mut pkgs: Vec<PackageDefinition> = Vec::new();
        for (name, target) in targets.signed.targets.iter() {
            let pkg_def = package_reader.read_package_definition(&name, &target.custom.merkle)?;
            pkgs.push(pkg_def);
        }
        pkgs.sort_by(|lhs, rhs| lhs.url.cmp(&rhs.url));
        Ok(pkgs)
    }

    /// Extends an existing service mapping with the services defined in a
    /// .config file found inside the config-data package.
    fn extend_service_mapping(
        service_mapping: &mut ServiceMapping,
        services: HashMap<String, Value>,
    ) {
        for (service_name, service_url_or_array) in services {
            if service_mapping.contains_key(&service_name) {
                debug!(
                    "Service mapping collision on {} between {} and {}",
                    service_name, service_mapping[&service_name], service_url_or_array
                );
            }

            let service_url: String;
            if service_url_or_array.is_array() {
                let service_array = service_url_or_array.as_array().unwrap();
                if service_array[0].is_string() {
                    service_url = service_array[0].as_str().unwrap().to_string();
                } else {
                    error!(
                        "Expected a string service url, instead got: {}:{}",
                        service_name, service_array[0]
                    );
                    continue;
                }
            } else if service_url_or_array.is_string() {
                service_url = service_url_or_array.as_str().unwrap().to_string();
            } else {
                error!(
                    "Unexpected service mapping found: {}:{}",
                    service_name, service_url_or_array
                );
                continue;
            }
            service_mapping.insert(service_name, service_url);
        }
    }

    /// Combine service name->url mappings defined in config-data.
    fn extract_config_data(
        &self,
        package_reader: &Box<dyn PackageReader>,
        served: &Vec<PackageDefinition>,
    ) -> Result<SysManagerConfig> {
        let mut sys_config =
            SysManagerConfig { services: ServiceMapping::new(), apps: HashSet::<String>::new() };

        for pkg_def in served {
            if pkg_def.url == CONFIG_DATA_PKG_URL {
                info!("Extracting config data");
                for (name, data) in &pkg_def.meta {
                    if SERVICE_CONFIG_RE.is_match(&name) {
                        info!("Reading service definition: {}", name);
                        let service_pkg =
                            package_reader.read_service_package_definition(data.to_string())?;
                        if let Some(apps) = service_pkg.apps {
                            for app in apps {
                                sys_config.apps.insert(app);
                            }
                        }
                        if let Some(services) = service_pkg.services {
                            Self::extend_service_mapping(&mut sys_config.services, services);
                        } else {
                            debug!("Expected service with name {} to exist. Optimistically continuing.", name);
                        }
                    }
                }
                break;
            }
        }
        Ok(sys_config)
    }

    /// Extracts the ZBI from the update package and parses it into the ZBI
    /// model.
    fn extract_zbi_from_update_package(
        getter: &Box<dyn PackageGetter>,
        package: &PackageDefinition,
    ) -> Result<Zbi> {
        info!("Extracting the ZBI from {}", package.url);
        for (path, merkle) in package.contents.iter() {
            if path == "zbi" || path == "zbi.signed" {
                let zbi_data = getter.read_raw(&format!("blobs/{}", merkle))?;
                let mut reader = ZbiReader::new(zbi_data);
                let sections = reader.parse()?;
                let mut bootfs = HashMap::new();
                let mut cmdline = String::new();
                info!("Extracted {} sections from the ZBI", sections.len());
                for section in sections.iter() {
                    info!("Extracted sections {:?}", section.section_type);
                    if section.section_type == ZbiType::StorageBootfs {
                        let mut bootfs_reader = BootfsReader::new(section.buffer.clone());
                        let bootfs_result = bootfs_reader.parse();
                        if let Err(err) = bootfs_result {
                            warn!("Bootfs parse failed {}", err);
                        } else {
                            bootfs = bootfs_result.unwrap();
                            info!("Bootfs found {} files", bootfs.len());
                        }
                    } else if section.section_type == ZbiType::Cmdline {
                        let mut cmd_str = std::str::from_utf8(&section.buffer)?;
                        if let Some(stripped) = cmd_str.strip_suffix("\u{0000}") {
                            cmd_str = stripped;
                        }
                        cmdline.push_str(&cmd_str);
                    }
                }
                return Ok(Zbi { sections, bootfs, cmdline });
            }
        }
        return Err(anyhow!("Unable to find a zbi file in the package."));
    }

    /// Extracts all of the components and manifests from a package.
    fn extract_package_data(
        component_id: &mut i32,
        service_map: &mut ServiceMapping,
        components: &mut HashMap<String, Component>,
        manifests: &mut Vec<Manifest>,
        pkg: &PackageDefinition,
    ) -> Result<()> {
        // Extract V1 and V2 components from the packages.
        for (path, cm) in &pkg.cms {
            // Component Framework Version 2.
            if path.starts_with("meta/") && path.ends_with(".cm") {
                *component_id += 1;
                let url = format!("{}#{}", pkg.url, path);
                components.insert(
                    url.clone(),
                    Component { id: *component_id, url: url.clone(), version: 2, inferred: false },
                );

                let cf2_manifest = {
                    if let ComponentManifest::Version2(decl_bytes) = &cm {
                        let mut cap_uses = Vec::new();
                        let base64_bytes = base64::encode(&decl_bytes);

                        if let Ok(cm_decl) = decode_persistent::<fsys::ComponentDecl>(&decl_bytes) {
                            if let Err(err) = cm_fidl_validator::validate(&cm_decl) {
                                warn!("Invalid cm {} {}", url, err);
                            } else {
                                if let Some(uses) = cm_decl.uses {
                                    for use_ in uses {
                                        match &use_ {
                                            fsys::UseDecl::Protocol(protocol) => {
                                                if let Some(source_name) = &protocol.source_name {
                                                    cap_uses.push(Capability::Protocol(
                                                        ProtocolCapability::new(
                                                            source_name.clone(),
                                                        ),
                                                    ));
                                                }
                                            }
                                            _ => {}
                                        }
                                    }
                                }
                                if let Some(exposes) = cm_decl.exposes {
                                    for expose in exposes {
                                        match &expose {
                                            fsys::ExposeDecl::Protocol(protocol) => {
                                                if let Some(source_name) = &protocol.source_name {
                                                    if let Some(fsys::Ref::Self_(_)) =
                                                        &protocol.source
                                                    {
                                                        service_map.insert(
                                                            source_name.clone(),
                                                            url.clone(),
                                                        );
                                                    }
                                                }
                                            }
                                            _ => {}
                                        }
                                    }
                                }
                            }
                        } else {
                            warn!("cm failed to be decoded {}", url);
                        }
                        Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version2(base64_bytes),
                            uses: cap_uses,
                        }
                    } else {
                        Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version2(String::from("")),
                            uses: Vec::new(),
                        }
                    }
                };
                manifests.push(cf2_manifest);
            // Component Framework Version 1.
            } else if path.starts_with("meta/") && path.ends_with(".cmx") {
                *component_id += 1;
                let url = format!("{}#{}", pkg.url, path);
                components.insert(
                    url.clone(),
                    Component { id: *component_id, url: url.clone(), version: 1, inferred: false },
                );

                let cf1_manifest = {
                    if let ComponentManifest::Version1(sandbox) = &cm {
                        Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version1(serde_json::to_string(&sandbox)?),
                            uses: {
                                match sandbox.services.as_ref() {
                                    Some(svcs) => svcs
                                        .into_iter()
                                        .map(|e| {
                                            Capability::Protocol(ProtocolCapability::new(e.clone()))
                                        })
                                        .collect(),
                                    None => Vec::new(),
                                }
                            },
                        }
                    } else {
                        Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version1(String::from("")),
                            uses: Vec::new(),
                        }
                    }
                };
                manifests.push(cf1_manifest);
            }
        }
        Ok(())
    }

    /// Extracts all the components and manifests from the ZBI.
    fn extract_zbi_data(
        component_id: &mut i32,
        service_map: &mut ServiceMapping,
        components: &mut HashMap<String, Component>,
        manifests: &mut Vec<Manifest>,
        zbi: &Zbi,
    ) -> Result<()> {
        for (file_name, file_data) in zbi.bootfs.iter() {
            if file_name.ends_with(".cm") {
                info!("Extracting bootfs manifest: {}", file_name);
                let url = BootUrl::new_resource("/".to_string(), file_name.to_string())?;
                let base64_bytes = base64::encode(&file_data);
                if let Ok(cm_decl) = decode_persistent::<fsys::ComponentDecl>(&file_data) {
                    if let Err(err) = cm_fidl_validator::validate(&cm_decl) {
                        warn!("Invalid cm {} {}", file_name, err);
                    } else {
                        let mut cap_uses = Vec::new();
                        if let Some(uses) = cm_decl.uses {
                            for use_ in uses {
                                match &use_ {
                                    fsys::UseDecl::Protocol(protocol) => {
                                        if let Some(source_name) = &protocol.source_name {
                                            cap_uses.push(Capability::Protocol(
                                                ProtocolCapability::new(source_name.clone()),
                                            ));
                                        }
                                    }
                                    _ => {}
                                }
                            }
                        }
                        if let Some(exposes) = cm_decl.exposes {
                            for expose in exposes {
                                match &expose {
                                    fsys::ExposeDecl::Protocol(protocol) => {
                                        if let Some(source_name) = &protocol.source_name {
                                            if let Some(fsys::Ref::Self_(_)) = &protocol.source {
                                                service_map
                                                    .insert(source_name.clone(), url.to_string());
                                            }
                                        }
                                    }
                                    _ => {}
                                }
                            }
                        }

                        // The root manifest is special semantically as it offers from its parent
                        // which is outside of the component model. So in this case offers
                        // should also be captured.
                        if file_name == ROOT_RESOURCE {
                            if let Some(offers) = cm_decl.offers {
                                for offer in offers {
                                    match &offer {
                                        fsys::OfferDecl::Protocol(protocol) => {
                                            if let Some(source_name) = &protocol.source_name {
                                                if let Some(fsys::Ref::Parent(_)) = &protocol.source
                                                {
                                                    service_map.insert(
                                                        source_name.clone(),
                                                        url.to_string(),
                                                    );
                                                }
                                            }
                                        }
                                        _ => {}
                                    }
                                }
                            }
                        }

                        // Add the components directly from the ZBI.
                        *component_id += 1;
                        components.insert(
                            url.to_string(),
                            Component {
                                id: *component_id,
                                url: url.to_string(),
                                version: 2,
                                inferred: false,
                            },
                        );
                        manifests.push(Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version2(base64_bytes),
                            uses: cap_uses,
                        });
                    }
                }
            }
        }

        Ok(())
    }

    /// Iterate through all services mappings, for each one, find the associated node or create a new
    /// inferred node and mark it as a provider of that service.
    fn infer_components(
        component_id: &mut i32,
        service_map: &mut ServiceMapping,
        components: &mut HashMap<String, Component>,
    ) {
        for (service_name, pkg_url) in service_map.iter() {
            if !components.contains_key(pkg_url) {
                // We don't already know about the component that *should* provide this service.
                // Create an inferred node.
                debug!("Expected component {} exist to provide service {} but it does not exist. Creating inferred node.", pkg_url, service_name);
                *component_id += 1;
                components.insert(
                    pkg_url.clone(),
                    Component {
                        id: *component_id,
                        url: pkg_url.clone(),
                        version: 1,
                        inferred: true,
                    },
                );
            }
        }
    }

    /// Iterate through all nodes created thus far, creating edges between them based on the services they use.
    /// If a service provider node is not able to be found, create a new inferred service provider node.
    /// Since manifests more naturally hold the list of services that the component requires, we iterate through
    /// those instead. Can be changed relatively effortlessly if the model make sense otherwise.
    fn generate_routes(
        component_id: &mut i32,
        service_map: &mut ServiceMapping,
        components: &mut HashMap<String, Component>,
        manifests: &Vec<Manifest>,
        routes: &mut Vec<Route>,
    ) {
        let mut route_idx = 0;
        for mani in manifests {
            for capability in &mani.uses {
                if let Capability::Protocol(cap) = capability {
                    let service_name = &cap.source_name;
                    let target_id = {
                        if service_map.contains_key(service_name) {
                            // FIXME: Options do not impl Try so we cannot ? but there must be some better way to get at a value...
                            components.get(service_map.get(service_name).unwrap()).unwrap().id
                        } else {
                            // Even the service map didn't know about this service. We should create an inferred component
                            // that provides this service.
                            debug!("Expected a service provider for service {} but it does not exist. Creating inferred node.", service_name);
                            *component_id += 1;
                            let url = format!("fuchsia-pkg://inferred#meta/{}.cmx", service_name);
                            components.insert(
                                url.clone(),
                                Component {
                                    id: *component_id,
                                    url: url.clone(),
                                    version: 1,
                                    inferred: true,
                                },
                            );
                            // Add the inferred node to the service map to be found by future consumers of the service
                            service_map.insert(String::from(service_name), url);
                            *component_id
                        }
                    };
                    route_idx += 1;
                    routes.push(Route {
                        id: route_idx,
                        src_id: mani.component_id,
                        dst_id: target_id,
                        service_name: service_name.to_string(),
                        protocol_id: 0, // FIXME:
                    });
                }
            }
        }
    }

    /// Function to build the component graph model out of the packages and services retrieved
    /// by this collector.
    fn extract(
        artifact_getter: &Box<dyn PackageGetter>,
        fuchsia_packages: Vec<PackageDefinition>,
        mut service_map: ServiceMapping,
    ) -> Result<PackageDataResponse> {
        let mut components: HashMap<String, Component> = HashMap::new();
        let mut packages: Vec<Package> = Vec::new();
        let mut manifests: Vec<Manifest> = Vec::new();
        let mut routes: Vec<Route> = Vec::new();
        let mut zbi: Option<Zbi> = None;

        // Iterate through all served packages, for each cmx they define, create a node.
        let mut component_id = 0;
        info!("Found {} package", fuchsia_packages.len());
        for pkg in fuchsia_packages.iter() {
            info!("Extracting package: {}", pkg.url);
            let package = Package {
                url: pkg.url.clone(),
                merkle: pkg.merkle.clone(),
                contents: pkg.contents.clone(),
                meta: pkg.meta.clone(),
            };
            packages.push(package);

            // If the package is the update package attempt to extract the ZBI.
            if pkg.url == UPDATE_PKG_URL {
                let zbi_result =
                    PackageDataCollector::extract_zbi_from_update_package(&artifact_getter, &pkg);
                if let Err(err) = zbi_result {
                    warn!("{}", err);
                } else {
                    zbi = Some(zbi_result.unwrap());
                }
            }

            Self::extract_package_data(
                &mut component_id,
                &mut service_map,
                &mut components,
                &mut manifests,
                &pkg,
            )?;
        }

        if let Some(zbi) = &zbi {
            Self::extract_zbi_data(
                &mut component_id,
                &mut service_map,
                &mut components,
                &mut manifests,
                &zbi,
            )?;
        }

        Self::infer_components(&mut component_id, &mut service_map, &mut components);

        Self::generate_routes(
            &mut component_id,
            &mut service_map,
            &mut components,
            &manifests,
            &mut routes,
        );

        info!(
            "Components: {}, Manifests {}, Routes {}.",
            components.len(),
            manifests.len(),
            routes.len()
        );

        Ok(PackageDataResponse::new(components, packages, manifests, routes, zbi))
    }

    pub fn collect_with_reader(
        &self,
        package_reader: Box<dyn PackageReader>,
        artifact_reader: Box<dyn PackageGetter>,
        model: Arc<DataModel>,
    ) -> Result<()> {
        let served_packages = self.get_packages(&package_reader)?;
        let sysmgr_config = self.extract_config_data(&package_reader, &served_packages)?;

        info!(
            "Done collecting. Found in the sys realm: {} services, {} apps, {} served packages.",
            sysmgr_config.services.keys().len(),
            sysmgr_config.apps.len(),
            served_packages.len(),
        );

        let response = PackageDataCollector::extract(
            &artifact_reader,
            served_packages,
            sysmgr_config.services.clone(),
        )?;

        let mut model_comps = vec![];
        for (_, val) in response.components.into_iter() {
            model_comps.push(val);
        }

        model.set(Components::new(model_comps))?;
        model.set(Packages::new(response.packages))?;
        model.set(Manifests::new(response.manifests))?;
        model.set(Routes::new(response.routes))?;
        model.set(Sysmgr::new(sysmgr_config.services, sysmgr_config.apps.into_iter().collect()))?;

        if let Some(zbi) = response.zbi {
            model.set(zbi)?;
        } else {
            model.remove::<Zbi>()?;
        }

        Ok(())
    }
}

impl DataCollector for PackageDataCollector {
    /// Collects and builds a DAG of component nodes (with manifests) and routes that
    /// connect the nodes.
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let repository_path = model.config().repository_path();
        let package_reader: Box<dyn PackageReader> =
            Box::new(PackageServerReader::new(Box::new(ArtifactGetter::new(&repository_path))));
        let artifact_reader: Box<dyn PackageGetter> =
            Box::new(ArtifactGetter::new(&repository_path));

        self.collect_with_reader(package_reader, artifact_reader, model)?;

        Ok(())
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::core::package::test_utils::{
            create_model, create_svc_pkg_def, create_svc_pkg_def_with_array, create_test_cm_map,
            create_test_cmx_map, create_test_package_with_cms, create_test_package_with_contents,
            create_test_package_with_meta, create_test_sandbox, MockPackageGetter,
            MockPackageReader,
        },
        crate::core::util::jsons::*,
    };

    fn count_defined_inferred(components: HashMap<String, Component>) -> (usize, usize) {
        let mut defined_count = 0;
        let mut inferred_count = 0;
        for (_, comp) in components {
            if comp.inferred {
                inferred_count += 1;
            } else {
                defined_count += 1;
            }
        }
        (defined_count, inferred_count)
    }

    #[test]
    fn test_extract_config_data_ignores_services_defined_on_non_config_data_package() {
        // Create a single package that is NOT the config data package
        let mock_reader: Box<dyn PackageReader> = Box::new(MockPackageReader::new());

        let mut contents = HashMap::new();
        contents.insert(String::from("data/sysmgr/foo.config"), String::from("test_merkle"));
        let pkg = create_test_package_with_contents(
            String::from("fuchsia-pkg://fuchsia.com/not-config-data"),
            contents,
        );
        let served = vec![pkg];

        let collector = PackageDataCollector::default();
        let result = collector.extract_config_data(&mock_reader, &served).unwrap();
        assert_eq!(0, result.services.len());
        assert_eq!(0, result.apps.len())
    }

    #[test]
    fn test_extract_config_data_ignores_services_defined_by_non_config_meta_contents() {
        // Create a single package that IS the config data package but
        // does not contain valid data/sysmgr/*.config meta content.
        let mock_reader: Box<dyn PackageReader> = Box::new(MockPackageReader::new());

        let mut contents = HashMap::new();
        contents.insert(String::from("not/valid/config"), String::from("test_merkle"));
        let pkg = create_test_package_with_contents(String::from(CONFIG_DATA_PKG_URL), contents);
        let served = vec![pkg];

        let collector = PackageDataCollector::default();
        let result = collector.extract_config_data(&mock_reader, &served).unwrap();

        assert_eq!(0, result.services.len());
        assert_eq!(0, result.apps.len())
    }

    #[test]
    fn test_extract_config_data_takes_the_last_defined_duplicate_config_data_services() {
        let mock_reader = Box::new(MockPackageReader::new());
        // We will need 2 service package definitions that map the same service to different components
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![(
                String::from("fuchsia.test.foo.bar"),
                String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
            )],
            vec![],
        ));
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![(
                String::from("fuchsia.test.foo.bar"),
                String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
            )],
            vec![],
        ));
        let mut meta = HashMap::new();
        meta.insert(String::from("data/sysmgr/foo.config"), String::from("test_merkle"));
        meta.insert(String::from("data/sysmgr/bar.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_meta(String::from(CONFIG_DATA_PKG_URL), meta);
        let served = vec![pkg];

        let collector = PackageDataCollector::default();
        let package_reader: Box<dyn PackageReader> = mock_reader;
        let result = collector.extract_config_data(&package_reader, &served).unwrap();
        assert_eq!(1, result.services.len());
        assert!(result.services.contains_key("fuchsia.test.foo.bar"));
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx",
            result.services.get("fuchsia.test.foo.bar").unwrap()
        );
        assert_eq!(0, result.apps.len());
    }

    #[test]
    fn test_extract_config_data_merges_unique_service_names() {
        let mock_reader = Box::new(MockPackageReader::new());
        // We will need 2 service package definitions that map different services
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![(
                String::from("fuchsia.test.foo.service1"),
                String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
            )],
            vec![],
        ));
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![(
                String::from("fuchsia.test.foo.service2"),
                String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
            )],
            vec![],
        ));

        let mut meta = HashMap::new();
        meta.insert(String::from("data/sysmgr/service1.config"), String::from("test_merkle"));
        meta.insert(String::from("data/sysmgr/service2.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_meta(String::from(CONFIG_DATA_PKG_URL), meta);
        let served = vec![pkg];

        let collector = PackageDataCollector::default();
        let package_reader: Box<dyn PackageReader> = mock_reader;
        let result = collector.extract_config_data(&package_reader, &served).unwrap();
        assert_eq!(2, result.services.len());
        assert_eq!(0, result.apps.len());
    }

    #[test]
    fn test_extract_config_data_reads_first_value_when_given_an_array_for_service_url_mapping() {
        let mock_reader = Box::new(MockPackageReader::new());
        // Create 2 service map definitions that map different services
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![(
                String::from("fuchsia.test.foo.service1"),
                String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
            )],
            vec![],
        ));
        mock_reader.append_service_pkg_def(create_svc_pkg_def_with_array(
            vec![(
                String::from("fuchsia.test.foo.service2"),
                vec![
                    String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
                    String::from("--foo"),
                    String::from("--bar"),
                ],
            )],
            vec![],
        ));
        let mut meta = HashMap::new();
        meta.insert(String::from("data/sysmgr/service1.config"), String::from("test_merkle"));
        meta.insert(String::from("data/sysmgr/service2.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_meta(String::from(CONFIG_DATA_PKG_URL), meta);
        let served = vec![pkg];

        let collector = PackageDataCollector::default();
        let package_reader: Box<dyn PackageReader> = mock_reader;
        let result = collector.extract_config_data(&package_reader, &served).unwrap();
        assert_eq!(2, result.services.len());
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx",
            result.services.get("fuchsia.test.foo.service2").unwrap()
        );
    }

    #[test]
    fn test_extract_with_no_services_infers_service() {
        // Create a single test package with a single unknown service dependency
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        let served = vec![pkg];

        let services = HashMap::new();
        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let response = PackageDataCollector::extract(&package_getter, served, services).unwrap();

        assert_eq!(2, response.components.len());
        assert_eq!(1, response.manifests.len());
        assert_eq!(1, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
        assert_eq!((1, 1), count_defined_inferred(response.components)); // 1 real, 1 inferred
    }

    #[test]
    fn test_extract_with_known_services_but_no_matching_component_infers_component() {
        // Create a single test package with a single known service dependency
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        let served = vec![pkg];

        // We know about the desired service in the service mapping, but the component doesn't exist
        let mut services = HashMap::new();
        services.insert(
            String::from("fuchsia.test.foo.bar"),
            String::from("fuchsia-pkg://fuchsia.com/aries#meta/taurus.cmx"),
        );

        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let response = PackageDataCollector::extract(&package_getter, served, services).unwrap();

        assert_eq!(2, response.components.len());
        assert_eq!(1, response.manifests.len());
        assert_eq!(1, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
        assert_eq!((1, 1), count_defined_inferred(response.components)); // 1 real, 1 inferred
    }

    #[test]
    fn test_extract_with_invalid_cmx_creates_empty_graph() {
        // Create a single test package with an invalid cmx path
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let sb2 = create_test_sandbox(vec![String::from("fuchsia.test.foo.baz")]);
        let cms = create_test_cmx_map(vec![
            (String::from("foo/bar.cmx"), sb),
            (String::from("meta/baz"), sb2),
        ]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        let served = vec![pkg];

        let services = HashMap::new();

        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let response = PackageDataCollector::extract(&package_getter, served, services).unwrap();

        assert_eq!(0, response.components.len());
        assert_eq!(0, response.manifests.len());
        assert_eq!(0, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
        assert_eq!((0, 0), count_defined_inferred(response.components)); // 0 real, 0 inferred
    }

    #[test]
    fn test_extract_with_cm() {
        let cms = create_test_cm_map(vec![("meta/foo.cm".to_string(), vec![])]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        let served = vec![pkg];

        let services = HashMap::new();

        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let response = PackageDataCollector::extract(&package_getter, served, services).unwrap();

        assert_eq!(1, response.components.len());
        assert_eq!(response.components["fuchsia-pkg://fuchsia.com/foo#meta/foo.cm"].version, 2);
        assert_eq!(1, response.manifests.len());
        assert_eq!(0, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
    }

    #[test]
    fn test_extract_with_duplicate_inferred_services_reuses_inferred_service() {
        // Create two test packages that depend on the same inferred service
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);

        let sb2 = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms2 = create_test_cmx_map(vec![(String::from("meta/taurus.cmx"), sb2)]);
        let pkg2 =
            create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/aries"), cms2);
        let served = vec![pkg, pkg2];

        let services = HashMap::new();

        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let response = PackageDataCollector::extract(&package_getter, served, services).unwrap();

        assert_eq!(3, response.components.len());
        assert_eq!(2, response.manifests.len());
        assert_eq!(2, response.routes.len());
        assert_eq!(2, response.packages.len());
        assert_eq!(None, response.zbi);
        assert_eq!((2, 1), count_defined_inferred(response.components)); // 2 real, 1 inferred
    }

    #[test]
    fn test_extract_with_known_services_does_not_infer_service() {
        // Create two test packages, one that depends on a service provided by the other
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.taurus")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);

        let sb2 = create_test_sandbox(Vec::new());
        let cms2 = create_test_cmx_map(vec![(String::from("meta/taurus.cmx"), sb2)]);
        let pkg2 =
            create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/aries"), cms2);
        let served = vec![pkg, pkg2];

        // Map the service the first package requires to the second package
        let mut services = HashMap::new();
        services.insert(
            String::from("fuchsia.test.taurus"),
            String::from("fuchsia-pkg://fuchsia.com/aries#meta/taurus.cmx"),
        );

        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let response = PackageDataCollector::extract(&package_getter, served, services).unwrap();

        assert_eq!(2, response.components.len());
        assert_eq!(2, response.manifests.len());
        assert_eq!(1, response.routes.len());
        assert_eq!(2, response.packages.len());
        assert_eq!(None, response.zbi);
        assert_eq!((2, 0), count_defined_inferred(response.components)); // 2 real, 0 inferred
    }

    #[test]
    fn test_collect_clears_data_model_before_adding_new() {
        let mock_reader = Box::new(MockPackageReader::new());
        let (_, model) = create_model();
        // Put some "previous" content into the model.
        {
            let mut comps = vec![];
            comps.push(Component {
                id: 1,
                url: String::from("test.component"),
                version: 0,
                inferred: false,
            });
            comps.push(Component {
                id: 1,
                url: String::from("foo.bar"),
                version: 0,
                inferred: false,
            });
            model.set(Components { entries: comps }).unwrap();

            let mut manis = vec![];
            manis.push(crate::core::collection::Manifest {
                component_id: 1,
                manifest: ManifestData::Version1(String::from("test.component.manifest")),
                uses: vec![Capability::Protocol(ProtocolCapability::new(String::from(
                    "test.service",
                )))],
            });
            manis.push(crate::core::collection::Manifest {
                component_id: 2,
                manifest: ManifestData::Version1(String::from("foo.bar.manifest")),
                uses: Vec::new(),
            });
            model.set(Manifests { entries: manis }).unwrap();

            let mut routes = vec![];
            routes.push(Route {
                id: 1,
                src_id: 1,
                dst_id: 2,
                service_name: String::from("test.service"),
                protocol_id: 0,
            });
            model.set(Routes { entries: routes }).unwrap();
        }

        let mut targets = HashMap::new();
        targets.insert(
            String::from("123"),
            FarPackageDefinition { custom: Custom { merkle: String::from("123") } },
        );
        mock_reader.append_target(TargetsJson { signed: Signed { targets: targets } });
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        mock_reader.append_pkg_def(pkg);

        let reader: Box<dyn PackageReader> = mock_reader;
        let getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let collector = PackageDataCollector::default();
        collector.collect_with_reader(reader, getter, Arc::clone(&model)).unwrap();

        // Ensure the model reflects only the latest collection.
        let comps = &model.get::<Components>().unwrap().entries;
        let manis = &model.get::<Manifests>().unwrap().entries;
        let routes = &model.get::<Routes>().unwrap().entries;
        // There are 2 components (1 inferred, 1 defined),
        // 1 manifest (for the defined package), and 1 route
        assert_eq!(comps.len(), 2);
        assert_eq!(manis.len(), 1);
        assert_eq!(routes.len(), 1);
    }

    #[test]
    fn test_malformed_zbi() {
        let mut contents = HashMap::new();
        contents.insert(String::from("zbi"), String::from("000"));
        let pkg = create_test_package_with_contents(
            String::from("fuchsia-pkg://fuchsia.com/update"),
            contents,
        );
        let served = vec![pkg];
        let services = HashMap::new();

        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        let response = PackageDataCollector::extract(&package_getter, served, services).unwrap();
        assert_eq!(None, response.zbi);
    }

    #[test]
    fn test_packages_sorted() {
        let mock_reader = Box::new(MockPackageReader::new());
        let (_, model) = create_model();

        let mut targets = HashMap::new();
        targets.insert(
            String::from("123"),
            FarPackageDefinition { custom: Custom { merkle: String::from("123") } },
        );
        targets.insert(
            String::from("456"),
            FarPackageDefinition { custom: Custom { merkle: String::from("456") } },
        );

        mock_reader.append_target(TargetsJson { signed: Signed { targets: targets } });

        let sb_0 = create_test_sandbox(vec![String::from("fuchsia.test.foo")]);
        let cms_0 = create_test_cmx_map(vec![(String::from("meta/foo.cmx"), sb_0)]);
        let pkg_0 =
            create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms_0);
        mock_reader.append_pkg_def(pkg_0);

        let sb_1 = create_test_sandbox(vec![String::from("fuchsia.test.bar")]);
        let cms_1 = create_test_cmx_map(vec![(String::from("meta/bar.cmx"), sb_1)]);
        let pkg_1 =
            create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/bar"), cms_1);
        mock_reader.append_pkg_def(pkg_1);

        let collector = PackageDataCollector::default();
        let package_reader: Box<dyn PackageReader> = mock_reader;
        let package_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        collector.collect_with_reader(package_reader, package_getter, Arc::clone(&model)).unwrap();

        // Test that the packages are in sorted order.
        let packages = &model.get::<Packages>().unwrap().entries;
        assert_eq!(packages.len(), 2);
        assert_eq!(packages[0].url, "fuchsia-pkg://fuchsia.com/bar");
        assert_eq!(packages[1].url, "fuchsia-pkg://fuchsia.com/foo");
    }
    #[test]
    fn test_extract_config_data_with_only_duplicate_apps() {
        let mock_reader = Box::new(MockPackageReader::new());
        // We will need 2 service package definitions that map different services
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![],
            vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
        ));
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![],
            vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
        ));

        let collector = PackageDataCollector::default();

        let mut meta = HashMap::new();
        meta.insert(String::from("data/sysmgr/service1.config"), String::from("test_merkle"));
        meta.insert(String::from("data/sysmgr/service2.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_meta(String::from(CONFIG_DATA_PKG_URL), meta);
        let served = vec![pkg];

        let package_reader: Box<dyn PackageReader> = mock_reader;
        let result = collector.extract_config_data(&package_reader, &served).unwrap();
        assert_eq!(1, result.apps.len());
        assert!(result.apps.contains("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx"));
        assert_eq!(0, result.services.len());
    }

    #[test]
    fn test_extract_config_data_with_only_apps() {
        let mock_reader = Box::new(MockPackageReader::new());
        // We will need 2 service package definitions that map different services
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![],
            vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
        ));
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![],
            vec![String::from("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx")],
        ));

        let collector = PackageDataCollector::default();

        let mut meta = HashMap::new();
        meta.insert(String::from("data/sysmgr/service1.config"), String::from("test_merkle"));
        meta.insert(String::from("data/sysmgr/service2.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_meta(String::from(CONFIG_DATA_PKG_URL), meta);
        let served = vec![pkg];

        let package_reader: Box<dyn PackageReader> = mock_reader;
        let result = collector.extract_config_data(&package_reader, &served).unwrap();
        assert_eq!(2, result.apps.len());
        assert!(result.apps.contains("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx"));
        assert!(result.apps.contains("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx"));
        assert_eq!(0, result.services.len());
    }

    #[test]
    fn test_extract_config_data_with_apps_and_services() {
        let mock_reader = Box::new(MockPackageReader::new());
        // We will need 2 service package definitions that map different services
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![(
                String::from("fuchsia.test.foo.service1"),
                String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
            )],
            vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
        ));
        mock_reader.append_service_pkg_def(create_svc_pkg_def(
            vec![(
                String::from("fuchsia.test.foo.service2"),
                String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
            )],
            vec![String::from("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx")],
        ));

        let collector = PackageDataCollector::default();

        let mut meta = HashMap::new();
        meta.insert(String::from("data/sysmgr/service1.config"), String::from("test_merkle"));
        meta.insert(String::from("data/sysmgr/service2.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_meta(String::from(CONFIG_DATA_PKG_URL), meta);
        let served = vec![pkg];

        let package_reader: Box<dyn PackageReader> = mock_reader;
        let result = collector.extract_config_data(&package_reader, &served).unwrap();
        assert_eq!(2, result.apps.len());
        assert!(result.apps.contains("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx"));
        assert!(result.apps.contains("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx"));

        assert_eq!(2, result.services.len());
        assert!(result.services.contains_key("fuchsia.test.foo.service2"));
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx",
            result.services.get("fuchsia.test.foo.service2").unwrap()
        );
        assert!(result.services.contains_key("fuchsia.test.foo.service1"));
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx",
            result.services.get("fuchsia.test.foo.service1").unwrap()
        );
    }
}
