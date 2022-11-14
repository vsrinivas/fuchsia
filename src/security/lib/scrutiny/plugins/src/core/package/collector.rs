// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        core::{
            collection::{
                Capability, Component, ComponentSource, Components, CoreDataDeps, Manifest,
                ManifestData, Manifests, Package, Packages, ProtocolCapability, Route, Routes,
                Sysmgr, Zbi,
            },
            package::{
                is_cf_v1_manifest, is_cf_v2_manifest,
                reader::{PackageReader, PackagesFromUpdateReader},
            },
            util::types::{
                ComponentManifest, PackageDefinition, PartialPackageDefinition, ServiceMapping,
                SysManagerConfig, INFERRED_URL,
            },
        },
        static_pkgs::StaticPkgsCollection,
    },
    anyhow::{anyhow, Context, Result},
    cm_fidl_analyzer::{match_absolute_pkg_urls, PkgUrlMatch},
    cm_fidl_validator,
    fidl::encoding::decode_persistent,
    fidl_fuchsia_component_decl as fdecl,
    fuchsia_merkle::Hash,
    fuchsia_pkg_cache_url::{fuchsia_pkg_cache_component_url, pkg_cache_package_name_and_variant},
    fuchsia_url::{
        boot_url::BootUrl, AbsoluteComponentUrl, AbsolutePackageUrl, PackageName, PackageVariant,
    },
    once_cell::sync::Lazy,
    regex::Regex,
    scrutiny::model::{collector::DataCollector, model::DataModel},
    scrutiny_config::ModelConfig,
    scrutiny_utils::{
        artifact::{ArtifactReader, FileArtifactReader},
        bootfs::BootfsReader,
        zbi::{ZbiReader, ZbiType},
    },
    serde_json::Value,
    std::{
        collections::{HashMap, HashSet},
        path::{Path, PathBuf},
        str,
        sync::Arc,
    },
    tracing::{debug, error, info, warn},
    update_package::parse_image_packages_json,
    url::Url,
};

// Constants/Statics
static SERVICE_CONFIG_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"data/sysmgr/.+\.config").unwrap());
static ZBI_PATH: Lazy<PathBuf> = Lazy::new(|| PathBuf::from("zbi"));
static ZBI_SIGNED_PATH: Lazy<PathBuf> = Lazy::new(|| PathBuf::from("zbi.signed"));
static IMAGES_JSON_PATH: Lazy<PathBuf> = Lazy::new(|| PathBuf::from("images.json"));
static IMAGES_JSON_ORIG_PATH: Lazy<PathBuf> = Lazy::new(|| PathBuf::from("images.json.orig"));

// The root v2 component manifest.
pub const ROOT_RESOURCE: &str = "meta/root.cm";

struct StaticPackageDescription<'a> {
    pub name: &'a PackageName,
    pub variant: Option<&'a PackageVariant>,
    pub merkle: &'a Hash,
}

impl<'a> StaticPackageDescription<'a> {
    fn new(name: &'a PackageName, variant: Option<&'a PackageVariant>, merkle: &'a Hash) -> Self {
        Self { name, variant, merkle }
    }

    /// Definitions match when:
    /// 1. Package definition has no hash (warning emitted above), or hashes match;
    /// 2. Names match;
    /// 3. A variant is missing (warning emitted above), or variants match.
    fn matches(&self, pkg: &PackageDefinition) -> bool {
        let url = AbsolutePackageUrl::new(
            pkg.url.repository().clone(),
            self.name.clone(),
            self.variant.map(PackageVariant::clone),
            Some(self.merkle.clone()),
        );
        let url_match = match_absolute_pkg_urls(&url, &pkg.url);
        if url_match == PkgUrlMatch::WeakMatch {
            warn!(
                StaticPackageDescription.url = %url,
                PackageDefinition.url = %pkg.url,
                "Lossy match of absolute package URLs",
            );
        }
        url_match != PkgUrlMatch::NoMatch
    }
}

/// The PackageDataResponse contains all of the core model information extracted
/// from the Fuchsia Archive (.far) packages from the current build.
pub struct PackageDataResponse {
    pub components: HashMap<Url, Component>,
    pub packages: Vec<Package>,
    pub manifests: Vec<Manifest>,
    pub routes: Vec<Route>,
    pub zbi: Option<Zbi>,
}

impl PackageDataResponse {
    pub fn new(
        components: HashMap<Url, Component>,
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
    fn get_packages(package_reader: &mut Box<dyn PackageReader>) -> Result<Vec<PackageDefinition>> {
        let package_urls =
            package_reader.read_package_urls().context("Failed to read listing of package URLs")?;
        let mut pkgs = package_urls
            .into_iter()
            .map(|pkg_url| package_reader.read_package_definition(&pkg_url))
            .collect::<Result<Vec<PackageDefinition>>>()
            .context("Failed to read package definition")?;
        pkgs.sort_by(|lhs, rhs| lhs.url.name().cmp(&rhs.url.name()));
        Ok(pkgs)
    }

    /// Extends an existing service mapping with the services defined in a
    /// .config file found inside the config-data package.
    fn extend_service_mapping(
        service_mapping: &mut ServiceMapping,
        services: HashMap<String, Value>,
    ) -> Result<()> {
        for (service_name, service_url_or_array) in services {
            if service_mapping.contains_key(&service_name) {
                debug!(
                    %service_name,
                    "Service mapping collision between {} and {}",
                    service_mapping[&service_name], service_url_or_array
                );
            }

            let service_url_string: String;
            if service_url_or_array.is_array() {
                let service_array = service_url_or_array.as_array().unwrap();
                if service_array[0].is_string() {
                    service_url_string = service_array[0].as_str().unwrap().to_string();
                } else {
                    error!(
                        "Expected a string service url, instead got: {}:{}",
                        service_name, service_array[0]
                    );
                    continue;
                }
            } else if service_url_or_array.is_string() {
                service_url_string = service_url_or_array.as_str().unwrap().to_string();
            } else {
                error!(
                    "Unexpected service mapping found: {}:{}",
                    service_name, service_url_or_array
                );
                continue;
            }

            let service_url = Url::parse(&service_url_string).with_context(|| {
                format!(
                    "Failed to parse service URL {} mapped to service name {}",
                    &service_url_string, &service_name
                )
            })?;
            service_mapping.insert(service_name, service_url);
        }

        Ok(())
    }

    /// Combine service name->url mappings defined in config-data.
    fn extract_config_data(
        config_data_package_url: &AbsolutePackageUrl,
        package_reader: &mut Box<dyn PackageReader>,
        served: &Vec<PackageDefinition>,
    ) -> Result<SysManagerConfig> {
        let mut sys_config = SysManagerConfig {
            services: ServiceMapping::new(),
            apps: HashSet::<AbsoluteComponentUrl>::new(),
        };

        for pkg_def in served {
            if pkg_def.matches_url(config_data_package_url) {
                info!("Extracting config data");
                for (name, data) in &pkg_def.meta {
                    if let Some(name_str) = name.to_str() {
                        if SERVICE_CONFIG_RE.is_match(name_str) {
                            Self::extract_service_config(
                                package_reader,
                                &mut sys_config,
                                name,
                                data,
                            )?;
                        }
                    } else {
                        warn!(
                            ?name,
                            "Skipping internal package path that cannot be converted to string"
                        );
                    }
                }
                break;
            }
        }
        Ok(sys_config)
    }

    fn extract_service_config(
        package_reader: &mut Box<dyn PackageReader>,
        sys_config: &mut SysManagerConfig,
        name: &PathBuf,
        data: &Vec<u8>,
    ) -> Result<()> {
        info!(service_name = ?name, "Reading service definition");

        let service_pkg = package_reader.read_service_package_definition(data.as_slice())?;
        if let Some(apps) = service_pkg.apps {
            for app in apps {
                let app_url = AbsoluteComponentUrl::parse(&app).with_context(|| {
                    format!("Failed to parse sys manager config data app package URL: {}", app)
                })?;
                sys_config.apps.insert(app_url);
            }
        }
        if let Some(services) = service_pkg.services {
            Self::extend_service_mapping(&mut sys_config.services, services)?;
        } else {
            debug!(service_name = ?name, "Expected service to exist. Optimistically continuing.");
        }

        Ok(())
    }

    /// Extracts the ZBI from the update package and parses it into the ZBI
    /// model.
    fn extract_zbi_from_update_package(
        reader: &mut Box<dyn ArtifactReader>,
        update_package: &PartialPackageDefinition,
        fuchsia_packages: &Vec<PackageDefinition>,
    ) -> Result<Zbi> {
        info!("Extracting the ZBI from update package");

        let result_from_update_package = Self::lookup_zbi_hash_in_update_package(update_package);
        let result_from_images_json =
            Self::lookup_zbi_hash_in_images_json(reader, update_package, fuchsia_packages);
        let zbi_hash = match (result_from_update_package, result_from_images_json) {
            (Ok(zbi_hash_from_update_package), Ok(zbi_hash_from_images_json)) => {
                if zbi_hash_from_update_package != zbi_hash_from_images_json {
                    return Err(anyhow!(
                        "Update package and its images manifest contain different fuchsia ZBI images: {} != {}",
                        zbi_hash_from_update_package,
                        zbi_hash_from_images_json
                    ));
                }
                zbi_hash_from_images_json
            }
            (_, Ok(zbi_hash)) => zbi_hash,
            (Ok(zbi_hash), _) => zbi_hash,
            (_, Err(err_from_images_json)) => return Err(err_from_images_json),
        };

        let zbi_data = reader.read_bytes(&Path::new(&zbi_hash.to_string()))?;
        let mut reader = ZbiReader::new(zbi_data);
        let sections = reader.parse()?;
        let mut bootfs = HashMap::new();
        let mut cmdline = String::new();
        info!(total = sections.len(), "Extracted sections from the ZBI");
        for section in sections.iter() {
            info!(section_type = ?section.section_type, "Extracted sections");
            if section.section_type == ZbiType::StorageBootfs {
                let mut bootfs_reader = BootfsReader::new(section.buffer.clone());
                let bootfs_result = bootfs_reader.parse();
                if let Err(err) = bootfs_result {
                    warn!(%err, "Bootfs parse failed");
                } else {
                    bootfs = bootfs_result.unwrap();
                    info!(total = bootfs.len(), "Bootfs found files");
                }
            } else if section.section_type == ZbiType::Cmdline {
                let mut cmd_str = std::str::from_utf8(&section.buffer)?;
                if let Some(stripped) = cmd_str.strip_suffix("\u{0000}") {
                    cmd_str = stripped;
                }
                cmdline.push_str(&cmd_str);
            }
        }
        Ok(Zbi { sections, bootfs, cmdline })
    }

    fn lookup_zbi_hash_in_update_package(
        update_package: &PartialPackageDefinition,
    ) -> Result<Hash> {
        update_package
            .contents
            .get(&*ZBI_SIGNED_PATH)
            .or(update_package.contents.get(&*ZBI_PATH))
            .map(Hash::clone)
            .ok_or(anyhow!("Update package contains no zbi.signed or zbi entry"))
    }

    fn lookup_zbi_hash_in_images_json(
        reader: &mut Box<dyn ArtifactReader>,
        update_package: &PartialPackageDefinition,
        fuchsia_packages: &Vec<PackageDefinition>,
    ) -> Result<Hash> {
        let images_json_hash = update_package
            .contents
            .get(&*IMAGES_JSON_PATH)
            .or(update_package.contents.get(&*IMAGES_JSON_ORIG_PATH))
            .ok_or(anyhow!("Update package contains no images manifest entry"))?;
        let images_json_contents = reader
            .read_bytes(&Path::new(&images_json_hash.to_string()))
            .context("Failed to open images manifest blob designated in update package")?;
        let image_packages_manifest = parse_image_packages_json(images_json_contents.as_slice())
            .context("Failed to parse images manifest in update package")?;
        let fuchsia_metadata = image_packages_manifest.fuchsia().ok_or(anyhow!(
            "Update package images manifest contains no fuchsia boot slot images"
        ))?;
        let images_package_url = fuchsia_metadata.zbi().url();

        let images_package = fuchsia_packages
            .iter()
            .find(|&pkg_def| &pkg_def.url == images_package_url.clone().package_url())
            .ok_or_else(|| {
                anyhow!(
                    "Failed to locate update package images package with URL {} ",
                    **images_package_url
                )
            })?;
        images_package
            .contents
            .get(&PathBuf::from("zbi"))
            .map(Hash::clone)
            .ok_or(anyhow!("Update package images package contains no zbi.signed or zbi entry"))
    }

    fn get_non_bootfs_pkg_source<'a>(
        pkg: &'a PackageDefinition,
        static_pkgs: &'a Option<Vec<StaticPackageDescription<'a>>>,
    ) -> Result<ComponentSource> {
        let pkg_merkle = pkg.url.hash().ok_or_else(||
            anyhow!("Unable to report precise component source from package URL that is missing package hash: {}", pkg.url)
        )?;

        if static_pkgs.is_none() {
            return Ok(ComponentSource::Package(pkg_merkle.clone()));
        }

        for static_pkg in static_pkgs.as_ref().unwrap().iter() {
            if static_pkg.matches(pkg) {
                return Ok(ComponentSource::StaticPackage(static_pkg.merkle.clone()));
            }
        }

        Ok(ComponentSource::Package(pkg_merkle.clone()))
    }

    fn get_static_pkgs<'a>(
        static_pkgs_result: &'a Result<Arc<StaticPkgsCollection>>,
    ) -> Option<Vec<StaticPackageDescription<'a>>> {
        static_pkgs_result
            .as_ref()
            .ok()
            .map(|result| {
                // Collection is only meaningful if there are static packages and no errors.
                if result.static_pkgs.is_some() && result.errors.len() == 0 {
                    Some(
                        result
                            .static_pkgs
                            .as_ref()
                            .unwrap()
                            .iter()
                            .map(|((name, variant), merkle)| {
                                StaticPackageDescription::new(name, variant.as_ref(), merkle)
                            })
                            .collect(),
                    )
                } else {
                    None
                }
            })
            .unwrap_or(None)
    }

    fn get_static_pkg_deps(
        static_pkgs_result: &Result<Arc<StaticPkgsCollection>>,
    ) -> HashSet<PathBuf> {
        static_pkgs_result.as_ref().ok().map(|result| result.deps.clone()).unwrap_or(HashSet::new())
    }

    /// Extracts all of the components and manifests from a package.
    fn extract_package_data<'a>(
        component_id: &mut i32,
        service_map: &mut ServiceMapping,
        components: &mut HashMap<Url, Component>,
        manifests: &mut Vec<Manifest>,
        pkg: &PackageDefinition,
        static_pkgs: &'a Option<Vec<StaticPackageDescription<'a>>>,
    ) -> Result<()> {
        let source = Self::get_non_bootfs_pkg_source(pkg, static_pkgs)?;
        // Extract V1 and V2 components from the packages.
        for (path, cm) in &pkg.cms {
            let path_str = path.to_str().ok_or_else(|| {
                anyhow!("Cannot format component manifest path as string: {:?}", path)
            })?;
            // Component Framework Version 2.
            if is_cf_v2_manifest(path) {
                *component_id += 1;
                let url = AbsoluteComponentUrl::from_package_url_and_resource(
                    pkg.url.clone(),
                    path_str.to_string(),
                )
                .with_context(|| {
                    format!(
                        r#"Failed to apply resource path "{}" to package URL "{}""#,
                        path_str, pkg.url
                    )
                })?;
                let url = Url::parse(&url.to_string()).with_context(|| {
                    format!("Failed to convert package URL to standard URL: {}", url)
                })?;
                components.insert(
                    url.clone(),
                    Component {
                        id: *component_id,
                        url: url.clone(),
                        version: 2,
                        source: source.clone(),
                    },
                );

                let cf2_manifest = {
                    if let ComponentManifest::Version2(decl_bytes) = &cm {
                        let mut cap_uses = Vec::new();
                        let cm_base64 = base64::encode(&decl_bytes);
                        let mut cvf_bytes = None;

                        if let Ok(cm_decl) = decode_persistent::<fdecl::Component>(&decl_bytes) {
                            if let Err(err) = cm_fidl_validator::validate(&cm_decl) {
                                warn!(%err, %url, "Invalid cm");
                            } else {
                                if let Some(schema) = cm_decl.config {
                                    match schema
                                        .value_source
                                        .as_ref()
                                        .context("getting value source from config schema")?
                                    {
                                        fdecl::ConfigValueSource::PackagePath(pkg_path) => {
                                            cvf_bytes = Some(
                                                pkg.cvfs
                                                    .get(pkg_path)
                                                    .context("getting config values from package")?
                                                    .clone(),
                                            );
                                        }
                                        other => {
                                            warn!("unsupported config value source {:?}", other)
                                        }
                                    }
                                }

                                if let Some(uses) = cm_decl.uses {
                                    for use_ in uses {
                                        match &use_ {
                                            fdecl::Use::Protocol(protocol) => {
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
                                            fdecl::Expose::Protocol(protocol) => {
                                                if let Some(source_name) = &protocol.source_name {
                                                    if let Some(fdecl::Ref::Self_(_)) =
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
                            warn!(%url, "cm failed to be decoded");
                        }
                        Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version2 { cm_base64, cvf_bytes },
                            uses: cap_uses,
                        }
                    } else {
                        Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version2 {
                                cm_base64: String::from(""),
                                cvf_bytes: None,
                            },
                            uses: Vec::new(),
                        }
                    }
                };
                manifests.push(cf2_manifest);
            // Component Framework Version 1.
            } else if is_cf_v1_manifest(path) {
                *component_id += 1;
                let url = AbsoluteComponentUrl::from_package_url_and_resource(
                    pkg.url.clone(),
                    path_str.to_string(),
                )
                .with_context(|| {
                    format!(
                        r#"Failed to apply resource path "{}" to package URL "{}""#,
                        path_str, pkg.url
                    )
                })?;
                let url = Url::parse(&url.to_string()).with_context(|| {
                    format!("Failed to convert package URL to standard URL: {}", url)
                })?;
                components.insert(
                    url.clone(),
                    Component {
                        id: *component_id,
                        url: url.clone(),
                        version: 1,
                        source: source.clone(),
                    },
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
        components: &mut HashMap<Url, Component>,
        manifests: &mut Vec<Manifest>,
        zbi: &Zbi,
    ) -> Result<()> {
        for (file_name, file_data) in &zbi.bootfs {
            if file_name.ends_with(".cm") {
                info!(%file_name, "Extracting bootfs manifest");
                let url = BootUrl::new_resource("/".to_string(), file_name.to_string())?;
                let url = Url::parse(&url.to_string()).with_context(|| {
                    format!("Failed to convert boot URL to standard URL: {}", url)
                })?;
                let cm_base64 = base64::encode(&file_data);
                if let Ok(cm_decl) = decode_persistent::<fdecl::Component>(&file_data) {
                    if let Err(err) = cm_fidl_validator::validate(&cm_decl) {
                        warn!(%file_name, %err, "Invalid cm");
                    } else {
                        // Retrieve this component's config values, if any.
                        let cvf_bytes = if let Some(schema) = cm_decl.config {
                            match schema
                                .value_source
                                .as_ref()
                                .context("getting value source from config schema")?
                            {
                                fdecl::ConfigValueSource::PackagePath(pkg_path) => {
                                    zbi.bootfs.get(pkg_path).cloned()
                                }
                                other => {
                                    anyhow::bail!("Unsupported config value source {:?}.", other);
                                }
                            }
                        } else {
                            None
                        };

                        let mut cap_uses = Vec::new();
                        if let Some(uses) = cm_decl.uses {
                            for use_ in uses {
                                match &use_ {
                                    fdecl::Use::Protocol(protocol) => {
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
                                    fdecl::Expose::Protocol(protocol) => {
                                        if let Some(source_name) = &protocol.source_name {
                                            if let Some(fdecl::Ref::Self_(_)) = &protocol.source {
                                                service_map
                                                    .insert(source_name.clone(), url.clone());
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
                                        fdecl::Offer::Protocol(protocol) => {
                                            if let Some(source_name) = &protocol.source_name {
                                                if let Some(fdecl::Ref::Parent(_)) =
                                                    &protocol.source
                                                {
                                                    service_map
                                                        .insert(source_name.clone(), url.clone());
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
                            url.clone(),
                            Component {
                                id: *component_id,
                                url: url,
                                version: 2,
                                source: ComponentSource::ZbiBootfs,
                            },
                        );
                        manifests.push(Manifest {
                            component_id: *component_id,
                            manifest: ManifestData::Version2 { cm_base64, cvf_bytes },
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
        components: &mut HashMap<Url, Component>,
        manifests: &mut Vec<Manifest>,
    ) {
        for (service_name, pkg_url) in service_map.iter() {
            if !components.contains_key(pkg_url) {
                // We don't already know about the component that *should* provide this service.
                // Create an inferred node.
                debug!(
                    %pkg_url, %service_name,
                    "Expected component to exist to provide service, but it does not exist. Creating inferred node."
                );
                *component_id += 1;
                components.insert(
                    pkg_url.clone(),
                    Component {
                        id: *component_id,
                        url: pkg_url.clone(),
                        version: 1,
                        source: ComponentSource::Inferred,
                    },
                );
            }
        }

        Self::infer_fuchsia_pkg_cache_component(component_id, components, manifests);
    }

    /// Locate the `pkg-cache` component and duplicate it under the special
    /// `fuchsia-pkg-cache`-scheme URL.
    fn infer_fuchsia_pkg_cache_component(
        component_id: &mut i32,
        components: &mut HashMap<Url, Component>,
        manifests: &mut Vec<Manifest>,
    ) {
        let mut matching_components = vec![];
        for (url, component) in components.iter() {
            let component_url_result: Result<AbsoluteComponentUrl, _> = url.as_str().parse();
            if let Ok(component_url) = component_url_result {
                let opt_variant = component_url.variant();
                if let Some(variant) = opt_variant {
                    let name_and_variant = pkg_cache_package_name_and_variant();
                    if (component_url.name(), variant) == (&name_and_variant.0, &name_and_variant.1)
                    {
                        matching_components.push(component);
                    }
                }
            }
        }
        if matching_components.len() == 0 {
            warn!("Failed to locate pkg-cache component for fuchsia-pkg-cache scheme handling");
            return;
        }
        if matching_components.len() > 1 {
            warn!(
                total = matching_components.len(),
                "Located multiple pkg-cache components for fuchsia-pkg-cache scheme handling",
            );
        }
        let matching_component = matching_components[0];

        let matching_manifests: Vec<&Manifest> = manifests
            .iter()
            .filter(|manifest| manifest.component_id == matching_component.id)
            .collect();
        if matching_manifests.len() == 0 {
            warn!("Failed to locate pkg-cache manifest for fuchsia-pkg-cache scheme handling");
            return;
        }
        if matching_manifests.len() > 1 {
            warn!(
                total = matching_manifests.len(),
                "Located multiple pkg-cache manifests for fuchsia-pkg-cache scheme handling",
            );
        }
        let matching_manifest = matching_manifests[0];

        *component_id += 1;
        manifests.push(Manifest {
            component_id: *component_id,
            manifest: matching_manifest.manifest.clone(),
            uses: matching_manifest.uses.clone(),
        });
        components.insert(
            fuchsia_pkg_cache_component_url().clone(),
            Component {
                id: *component_id,
                url: fuchsia_pkg_cache_component_url().clone(),
                version: 2,
                source: ComponentSource::Inferred,
            },
        );
    }

    /// Iterate through all nodes created thus far, creating edges between them based on the services they use.
    /// If a service provider node is not able to be found, create a new inferred service provider node.
    /// Since manifests more naturally hold the list of services that the component requires, we iterate through
    /// those instead. Can be changed relatively effortlessly if the model make sense otherwise.
    fn generate_routes(
        component_id: &mut i32,
        service_map: &mut ServiceMapping,
        components: &mut HashMap<Url, Component>,
        manifests: &Vec<Manifest>,
        routes: &mut Vec<Route>,
    ) {
        let mut route_idx = 0;
        for mani in manifests {
            for capability in &mani.uses {
                if let Capability::Protocol(cap) = capability {
                    let service_name = &cap.source_name;
                    let source_component_id = {
                        if service_map.contains_key(service_name) {
                            // FIXME: Options do not impl Try so we cannot ? but there must be some better way to get at a value...
                            components.get(service_map.get(service_name).unwrap()).unwrap().id
                        } else {
                            // Even the service map didn't know about this service. We should create an inferred component
                            // that provides this service.
                            debug!(%service_name, "Expected a service provider for service but it does not exist. Creating inferred node.");
                            *component_id += 1;
                            let url_fragment = format!("#meta/{}.cmx", service_name);
                            let url = INFERRED_URL
                                .join(&url_fragment)
                                .expect("inferred URL can be joined with manifest fragment");
                            components.insert(
                                url.clone(),
                                Component {
                                    id: *component_id,
                                    url: url.clone(),
                                    version: 1,
                                    source: ComponentSource::Inferred,
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
                        src_id: source_component_id,
                        dst_id: mani.component_id,
                        service_name: service_name.to_string(),
                        protocol_id: 0, // FIXME:
                    });
                }
            }
        }
    }

    /// Function to build the component graph model out of the packages and services retrieved
    /// by this collector.
    fn extract<'a>(
        update_package: &PartialPackageDefinition,
        mut artifact_reader: &mut Box<dyn ArtifactReader>,
        fuchsia_packages: Vec<PackageDefinition>,
        mut service_map: ServiceMapping,
        static_pkgs: &'a Option<Vec<StaticPackageDescription<'a>>>,
    ) -> Result<PackageDataResponse> {
        let mut components: HashMap<Url, Component> = HashMap::new();
        let mut packages: Vec<Package> = Vec::new();
        let mut manifests: Vec<Manifest> = Vec::new();
        let mut routes: Vec<Route> = Vec::new();

        // Iterate through all served packages, for each cmx they define, create a node.
        let mut component_id = 0;
        info!(total = fuchsia_packages.len(), "Found package");
        for pkg in fuchsia_packages.iter() {
            info!(url = %pkg.url, "Extracting package");
            let merkle = pkg.url.hash().ok_or_else(||
                anyhow!("Unable to extract precise package information from URL without package hash: {}", pkg.url)
            )?.clone();
            let package = Package {
                name: pkg.url.name().clone(),
                variant: pkg.url.variant().map(|variant| variant.clone()),
                merkle,
                contents: pkg.contents.clone(),
                meta: pkg.meta.clone(),
            };
            packages.push(package);

            Self::extract_package_data(
                &mut component_id,
                &mut service_map,
                &mut components,
                &mut manifests,
                &pkg,
                &static_pkgs,
            )?;
        }

        let zbi = match PackageDataCollector::extract_zbi_from_update_package(
            &mut artifact_reader,
            update_package,
            &fuchsia_packages,
        ) {
            Ok(zbi) => {
                Self::extract_zbi_data(
                    &mut component_id,
                    &mut service_map,
                    &mut components,
                    &mut manifests,
                    &zbi,
                )?;
                Some(zbi)
            }
            Err(err) => {
                warn!(%err);
                None
            }
        };

        Self::infer_components(
            &mut component_id,
            &mut service_map,
            &mut components,
            &mut manifests,
        );

        Self::generate_routes(
            &mut component_id,
            &mut service_map,
            &mut components,
            &manifests,
            &mut routes,
        );

        info!(components = components.len(), manifests = manifests.len(), routes = routes.len());

        Ok(PackageDataResponse::new(components, packages, manifests, routes, zbi))
    }

    pub fn collect_with_reader(
        config: ModelConfig,
        mut package_reader: Box<dyn PackageReader>,
        mut artifact_reader: Box<dyn ArtifactReader>,
        model: Arc<DataModel>,
    ) -> Result<()> {
        let served_packages =
            Self::get_packages(&mut package_reader).context("Failed to read packages listing")?;
        let sysmgr_config = Self::extract_config_data(
            &config.config_data_package_url(),
            &mut package_reader,
            &served_packages,
        )
        .context("Failed to read sysmgr config")?;
        info!(
            services = sysmgr_config.services.keys().len(),
            apps = sysmgr_config.apps.len(),
            packages = served_packages.len(),
            "Done collecting. Found listed in the sys realm"
        );

        let update_package = package_reader
            .read_update_package_definition()
            .context("Failed to read update package definition for package data collector")?;
        let static_pkgs_result = model.get();
        let static_pkgs = Self::get_static_pkgs(&static_pkgs_result);
        let response = PackageDataCollector::extract(
            &update_package,
            &mut artifact_reader,
            served_packages,
            sysmgr_config.services.clone(),
            &static_pkgs,
        )?;

        let mut model_comps = vec![];
        for (_, val) in response.components.into_iter() {
            model_comps.push(val);
        }

        // TODO: Collections need to forward deps to controllers.
        model.set(Components::new(model_comps)).context("Failed to store components in model")?;
        model.set(Packages::new(response.packages)).context("Failed to store packages in model")?;
        model
            .set(Manifests::new(response.manifests))
            .context("Failed to store manifests in model")?;
        model.set(Routes::new(response.routes)).context("Failed to store routes in model")?;
        model
            .set(Sysmgr::new(sysmgr_config.services, sysmgr_config.apps))
            .context("Failed to store sysmgr config in model")?;

        if let Some(zbi) = response.zbi {
            model.set(zbi)?;
        } else {
            model.remove::<Zbi>();
        }

        let mut deps = Self::get_static_pkg_deps(&static_pkgs_result);
        for dep in package_reader.get_deps().into_iter() {
            deps.insert(dep);
        }
        for dep in artifact_reader.get_deps().into_iter() {
            deps.insert(dep);
        }
        model.set(CoreDataDeps::new(deps)).context("Failed to store core data deps")?;

        Ok(())
    }
}

impl DataCollector for PackageDataCollector {
    /// Collects and builds a DAG of component nodes (with manifests) and routes that
    /// connect the nodes.
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let model_config = model.config();
        let blobs_directory = &model_config.blobs_directory();
        let artifact_reader_for_artifact_reader =
            FileArtifactReader::new(&PathBuf::new(), blobs_directory);
        let artifact_reader_for_package_reader = artifact_reader_for_artifact_reader.clone();

        let package_reader: Box<dyn PackageReader> = Box::new(PackagesFromUpdateReader::new(
            &model_config.update_package_path(),
            Box::new(artifact_reader_for_package_reader),
        ));

        Self::collect_with_reader(
            model.config().clone(),
            package_reader,
            Box::new(artifact_reader_for_artifact_reader),
            model,
        )?;

        Ok(())
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::{PackageDataCollector, StaticPackageDescription},
        crate::core::{
            collection::{
                testing::fake_component_src_pkg, Capability, Component, ComponentSource,
                Components, CoreDataDeps, ManifestData, Manifests, Packages, ProtocolCapability,
                Route, Routes,
            },
            package::{
                reader::PackageReader,
                test_utils::{
                    create_model, create_svc_pkg_bytes, create_svc_pkg_bytes_with_array,
                    create_test_cm_map, create_test_cmx_map, create_test_package_with_cms,
                    create_test_package_with_contents, create_test_package_with_meta,
                    create_test_partial_package_with_contents, create_test_sandbox,
                    MockPackageReader,
                },
            },
            util::types::{PackageDefinition, PartialPackageDefinition, INFERRED_URL},
        },
        fuchsia_hash::{Hash, HASH_SIZE},
        fuchsia_merkle::MerkleTree,
        fuchsia_url::{AbsoluteComponentUrl, AbsolutePackageUrl, PackageName, PackageVariant},
        fuchsia_zbi_abi::zbi_container_header,
        maplit::{hashmap, hashset},
        scrutiny_testing::{artifact::MockArtifactReader, fake::fake_model_config},
        scrutiny_utils::artifact::ArtifactReader,
        sha2::{Digest, Sha256},
        std::{collections::HashMap, convert::TryInto, path::PathBuf, str::FromStr, sync::Arc},
        update_package::{ImageMetadata, ImagePackagesManifest},
        url::Url,
        zerocopy::AsBytes,
    };

    fn zero_content_zbi() -> Vec<u8> {
        zbi_container_header(0).as_bytes().into()
    }

    // Return the sha256 content hash of bytes, not to be confused with the fuchsia merkle root.
    fn content_hash(bytes: &[u8]) -> Hash {
        let mut hasher = Sha256::new();
        hasher.update(bytes);
        Hash::from(*AsRef::<[u8; 32]>::as_ref(&hasher.finalize()))
    }

    fn default_pkg() -> PackageDefinition {
        PackageDefinition {
            url: "fuchsia-pkg://fuchsia.com/default/0?hash=0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
            meta: HashMap::new(),
            contents: HashMap::new(),
            cms: HashMap::new(),
            cvfs: HashMap::new(),
        }
    }

    fn empty_update_pkg() -> PartialPackageDefinition {
        PartialPackageDefinition {
            meta: HashMap::new(),
            contents: HashMap::new(),
            cms: HashMap::new(),
            cvfs: HashMap::new(),
        }
    }

    #[fuchsia::test]
    fn test_static_pkgs_matches() {
        let pkg_def_with_variant = PackageDefinition {
            url: "fuchsia-pkg://fuchsia.com/alpha-beta_gamma9/0?hash=0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
            ..default_pkg()
        };
        let pkg_def_without_variant = PackageDefinition {
            url: "fuchsia-pkg://fuchsia.com/alpha-beta_gamma9?hash=0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
            ..default_pkg()
        };
        // Match.
        assert!(StaticPackageDescription::new(
            &PackageName::from_str("alpha-beta_gamma9").unwrap(),
            Some(&PackageVariant::zero()),
            &Hash::from([0u8; HASH_SIZE])
        )
        .matches(&pkg_def_with_variant));
        // Match with self variant None, input variant Some.
        assert!(StaticPackageDescription::new(
            &PackageName::from_str("alpha-beta_gamma9").unwrap(),
            None,
            &Hash::from([0u8; HASH_SIZE])
        )
        .matches(&pkg_def_with_variant));
        // Match with self variant Some, input variant None.
        assert!(StaticPackageDescription::new(
            &PackageName::from_str("alpha-beta_gamma9").unwrap(),
            Some(&PackageVariant::zero()),
            &Hash::from([0u8; HASH_SIZE])
        )
        .matches(&pkg_def_without_variant));
        // Variant mismatch.
        assert!(
            StaticPackageDescription::new(
                &PackageName::from_str("alpha-beta_gamma9").unwrap(),
                Some(&PackageVariant::from_str("1").unwrap()),
                &Hash::from([0u8; HASH_SIZE])
            )
            .matches(&pkg_def_with_variant)
                == false
        );
        // Merkle mismatch.
        assert!(
            StaticPackageDescription::new(
                &PackageName::from_str("alpha").unwrap(),
                Some(&PackageVariant::zero()),
                &Hash::from([1u8; HASH_SIZE])
            )
            .matches(&pkg_def_with_variant)
                == false
        );
    }

    fn count_sources(components: HashMap<Url, Component>) -> (usize, usize, usize, usize) {
        let mut inferred_count = 0;
        let mut zbi_bootfs_count = 0;
        let mut package_count = 0;
        let mut static_package_count = 0;
        for (_, comp) in components {
            match comp.source {
                ComponentSource::Inferred => {
                    inferred_count += 1;
                }
                ComponentSource::ZbiBootfs => {
                    zbi_bootfs_count += 1;
                }
                ComponentSource::Package(_) => {
                    package_count += 1;
                }
                ComponentSource::StaticPackage(_) => {
                    static_package_count += 1;
                }
            }
        }
        (inferred_count, zbi_bootfs_count, package_count, static_package_count)
    }

    #[fuchsia::test]
    fn test_extract_config_data_ignores_services_defined_on_non_config_data_package() {
        // Create a single package that is NOT the config data package
        let mut mock_reader: Box<dyn PackageReader> = Box::new(MockPackageReader::new());

        let mut contents = HashMap::new();
        contents.insert(PathBuf::from("data/sysmgr/foo.config"), Hash::from([0u8; HASH_SIZE]));
        let pkg = create_test_package_with_contents(
            PackageName::from_str("not-config-data").unwrap(),
            None,
            contents,
        );
        let served = vec![pkg];

        let config = fake_model_config();
        let result = PackageDataCollector::extract_config_data(
            &config.config_data_package_url(),
            &mut mock_reader,
            &served,
        )
        .unwrap();
        assert_eq!(0, result.services.len());
        assert_eq!(0, result.apps.len())
    }

    #[fuchsia::test]
    fn test_extract_config_data_ignores_services_defined_by_non_config_meta_contents() {
        // Create a single package that IS the config data package but
        // does not contain valid data/sysmgr/*.config meta content.
        let mut mock_reader: Box<dyn PackageReader> = Box::new(MockPackageReader::new());

        let mut contents = HashMap::new();
        contents.insert(PathBuf::from("not/valid/config"), Hash::from([0u8; HASH_SIZE]));
        let pkg = create_test_package_with_contents(
            PackageName::from_str("config-data").unwrap(),
            None,
            contents,
        );
        let served = vec![pkg];

        let config = fake_model_config();
        let result = PackageDataCollector::extract_config_data(
            &config.config_data_package_url(),
            &mut mock_reader,
            &served,
        )
        .unwrap();

        assert_eq!(0, result.services.len());
        assert_eq!(0, result.apps.len())
    }

    #[fuchsia::test]
    fn test_extract_config_data_merges_unique_service_names() {
        let mock_reader = Box::new(MockPackageReader::new());

        // We will need 2 service package definitions that map different services
        let mut meta = HashMap::new();
        meta.insert(
            PathBuf::from("data/sysmgr/service1.config"),
            create_svc_pkg_bytes(
                vec![(
                    String::from("fuchsia.test.foo.service1"),
                    String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
                )],
                vec![],
            ),
        );
        meta.insert(
            PathBuf::from("data/sysmgr/service2.config"),
            create_svc_pkg_bytes(
                vec![(
                    String::from("fuchsia.test.foo.service2"),
                    String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
                )],
                vec![],
            ),
        );
        let config = fake_model_config();
        let pkg = create_test_package_with_meta(
            PackageName::from_str("config-data").unwrap(),
            None,
            meta,
        );
        let served = vec![pkg];

        let mut package_reader: Box<dyn PackageReader> = mock_reader;
        let result = PackageDataCollector::extract_config_data(
            &config.config_data_package_url(),
            &mut package_reader,
            &served,
        )
        .unwrap();
        assert_eq!(2, result.services.len());
        assert_eq!(0, result.apps.len());
    }

    #[fuchsia::test]
    fn test_extract_config_data_reads_first_value_when_given_an_array_for_service_url_mapping() {
        let served2_str = "fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx";
        let served2_url = Url::parse(served2_str).unwrap();
        let mock_reader = Box::new(MockPackageReader::new());
        let mut meta = HashMap::new();
        meta.insert(
            PathBuf::from("data/sysmgr/service1.config"),
            create_svc_pkg_bytes(
                vec![(
                    String::from("fuchsia.test.foo.service1"),
                    String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
                )],
                vec![],
            ),
        );
        meta.insert(
            PathBuf::from("data/sysmgr/service2.config"),
            create_svc_pkg_bytes_with_array(
                vec![(
                    String::from("fuchsia.test.foo.service2"),
                    vec![String::from(served2_str), String::from("--foo"), String::from("--bar")],
                )],
                vec![],
            ),
        );
        let config = fake_model_config();
        let pkg = create_test_package_with_meta(
            PackageName::from_str("config-data").unwrap(),
            None,
            meta,
        );
        let served = vec![pkg];

        let mut package_reader: Box<dyn PackageReader> = mock_reader;
        let result = PackageDataCollector::extract_config_data(
            &config.config_data_package_url(),
            &mut package_reader,
            &served,
        )
        .unwrap();
        assert_eq!(2, result.services.len());
        assert_eq!(&served2_url, result.services.get("fuchsia.test.foo.service2").unwrap());
    }

    #[fuchsia::test]
    fn test_extract_with_no_services_infers_service() {
        // Create a single test package with a single unknown service dependency
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);
        let served = vec![pkg];

        let services = HashMap::new();
        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        assert_eq!(2, response.components.len());
        assert_eq!(1, response.manifests.len());
        assert_eq!(1, response.routes.len());
        // Component 2 is inferred, providing the fuchsia.test.foo.bar service to component 1.
        assert_eq!(
            vec![Route {
                id: 1,
                src_id: 2,
                dst_id: 1,
                service_name: String::from("fuchsia.test.foo.bar"),
                protocol_id: 0
            }],
            response.routes
        );
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
        // 1 inferred, 0 zbi/bootfs, 1 (non-static) package, 0 static packages.
        assert_eq!((1, 0, 1, 0), count_sources(response.components));
    }

    #[fuchsia::test]
    fn test_extract_with_static_pkg() {
        // Create a single test package with a single unknown service dependency
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);
        let served = vec![pkg];

        let services = HashMap::new();
        let mut package_getter: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let pkg_name = PackageName::from_str("foo").unwrap();
        let pkg_variant = PackageVariant::zero();
        let pkg_hash = Hash::from([0u8; HASH_SIZE]);
        let static_pkgs =
            Some(vec![StaticPackageDescription::new(&pkg_name, Some(&pkg_variant), &pkg_hash)]);
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut package_getter,
            served,
            services,
            &static_pkgs,
        )
        .unwrap();

        assert_eq!(2, response.components.len());
        assert_eq!(1, response.manifests.len());
        assert_eq!(1, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
        // 1 inferred, 0 zbi/bootfs, 0 (non-static) packages, 1 static package.
        assert_eq!((1, 0, 0, 1), count_sources(response.components));
    }

    #[fuchsia::test]
    fn test_extract_with_known_services_but_no_matching_component_infers_component() {
        // Create a single test package with a single known service dependency
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);
        let served = vec![pkg];

        // We know about the desired service in the service mapping, but the component doesn't exist
        let mut services = HashMap::new();
        services.insert(
            String::from("fuchsia.test.foo.bar"),
            Url::parse("fuchsia-pkg://fuchsia.com/aries#meta/taurus.cmx").unwrap(),
        );

        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        assert_eq!(2, response.components.len());
        assert_eq!(1, response.manifests.len());
        assert_eq!(1, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
        // 1 inferred, 0 zbi/bootfs, 1 (non-static) package, 0 static packages.
        assert_eq!((1, 0, 1, 0), count_sources(response.components));
    }

    #[fuchsia::test]
    fn test_extract_with_invalid_cmx_creates_empty_graph() {
        // Create a single test package with an invalid cmx path
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let sb2 = create_test_sandbox(vec![String::from("fuchsia.test.foo.baz")]);
        let cms = create_test_cmx_map(vec![
            (PathBuf::from("foo/bar.cmx"), sb),
            (PathBuf::from("meta/baz"), sb2),
        ]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);
        let served = vec![pkg];

        let services = HashMap::new();

        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        assert_eq!(0, response.components.len());
        assert_eq!(0, response.manifests.len());
        assert_eq!(0, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
        // 0 inferred, 0 zbi/bootfs, 0 (non-static) package, 0 static packages.
        assert_eq!((0, 0, 0, 0), count_sources(response.components));
    }

    #[fuchsia::test]
    fn test_extract_with_cm() {
        let cms = create_test_cm_map(vec![(PathBuf::from("meta/foo.cm"), vec![])]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);
        let pkg_url = pkg.url.clone();
        let served = vec![pkg];

        let services = HashMap::new();

        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        let component_url = AbsoluteComponentUrl::from_package_url_and_resource(
            pkg_url.clone(),
            "meta/foo.cm".to_string(),
        )
        .unwrap();
        let url = Url::parse(&component_url.to_string()).unwrap();

        assert_eq!(1, response.components.len());
        assert_eq!(response.components[&url].version, 2);
        assert_eq!(1, response.manifests.len());
        assert_eq!(0, response.routes.len());
        assert_eq!(1, response.packages.len());
        assert_eq!(None, response.zbi);
    }

    #[fuchsia::test]
    fn test_extract_with_duplicate_inferred_services_reuses_inferred_service() {
        // Create two test packages that depend on the same inferred service
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);

        let sb2 = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms2 = create_test_cmx_map(vec![(PathBuf::from("meta/taurus.cmx"), sb2)]);
        let pkg2 =
            create_test_package_with_cms(PackageName::from_str("aries").unwrap(), None, cms2);
        let served = vec![pkg, pkg2];

        let services = HashMap::new();

        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        assert_eq!(3, response.components.len());
        assert_eq!(2, response.manifests.len());
        assert_eq!(2, response.routes.len());
        assert_eq!(2, response.packages.len());
        assert_eq!(None, response.zbi);
        // 1 inferred, 0 zbi/bootfs, 2 (non-static) packages, 0 static packages.
        assert_eq!((1, 0, 2, 0), count_sources(response.components));
    }

    #[fuchsia::test]
    fn test_extract_with_known_services_does_not_infer_service() {
        // Create two test packages, one that depends on a service provided by the other
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.taurus")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);

        let sb2 = create_test_sandbox(Vec::new());
        let cms2 = create_test_cmx_map(vec![(PathBuf::from("meta/taurus.cmx"), sb2)]);
        let pkg2 =
            create_test_package_with_cms(PackageName::from_str("aries").unwrap(), None, cms2);
        let served = vec![pkg, pkg2];

        // Map the service the first package requires to the second package
        let mut services = HashMap::new();
        services.insert(
            String::from("fuchsia.test.taurus"),
            Url::parse("fuchsia-pkg://test.fuchsia.com/aries?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/taurus.cmx").unwrap(),
        );

        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        tracing::info!(?response.components);

        assert_eq!(2, response.components.len());
        assert_eq!(2, response.manifests.len());
        assert_eq!(1, response.routes.len());
        assert_eq!(2, response.packages.len());
        assert_eq!(None, response.zbi);
        // 0 inferred, 0 zbi/bootfs, 2 (non-static) packages, 0 static packages.
        assert_eq!((0, 0, 2, 0), count_sources(response.components));
    }

    #[fuchsia::test]
    fn test_collect_clears_data_model_before_adding_new() {
        let mut mock_pkg_reader = Box::new(MockPackageReader::new());
        let (_, model) = create_model();
        // Put some "previous" content into the model.
        {
            let mut comps = vec![];
            comps.push(Component {
                id: 1,
                url: Url::parse("fuchsia-pkg://test.fuchsia.com/test?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/test.component.cmx").unwrap(),
                version: 0,
                source: ComponentSource::ZbiBootfs,
            });
            comps.push(Component {
                id: 1,
                url: Url::parse("fuchsia-pkg://test.fuchsia.com/test?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/foo.bar.cmx").unwrap(),
                version: 0,
                source: fake_component_src_pkg(),
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

        let sb = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);
        let pkg_urls = vec![pkg.url.clone()];
        mock_pkg_reader.append_update_package(pkg_urls, empty_update_pkg());
        mock_pkg_reader.append_pkg_def(pkg);

        let mock_artifact_reader = Box::new(MockArtifactReader::new());
        PackageDataCollector::collect_with_reader(
            fake_model_config(),
            mock_pkg_reader,
            mock_artifact_reader,
            Arc::clone(&model),
        )
        .unwrap();

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

    #[fuchsia::test]
    fn test_malformed_zbi() {
        let mut contents = HashMap::new();
        contents.insert(PathBuf::from("zbi"), Hash::from([0u8; HASH_SIZE]));
        let pkg = create_test_package_with_contents(
            PackageName::from_str("update").unwrap(),
            Some(PackageVariant::zero()),
            contents,
        );
        let served = vec![pkg];
        let services = HashMap::new();

        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();
        assert_eq!(None, response.zbi);
    }

    #[fuchsia::test]
    fn test_packages_sorted() {
        let mut mock_pkg_reader = Box::new(MockPackageReader::new());
        let (_, model) = create_model();

        let sb_0 = create_test_sandbox(vec![String::from("fuchsia.test.foo")]);
        let cms_0 = create_test_cmx_map(vec![(PathBuf::from("meta/foo.cmx"), sb_0)]);
        let pkg_0 =
            create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms_0);
        let pkg_0_url = pkg_0.url.clone();
        mock_pkg_reader.append_pkg_def(pkg_0);

        let sb_1 = create_test_sandbox(vec![String::from("fuchsia.test.bar")]);
        let cms_1 = create_test_cmx_map(vec![(PathBuf::from("meta/bar.cmx"), sb_1)]);
        let pkg_1 =
            create_test_package_with_cms(PackageName::from_str("bar").unwrap(), None, cms_1);
        let pkg_1_url = pkg_1.url.clone();
        mock_pkg_reader.append_pkg_def(pkg_1);

        let pkg_urls = vec![pkg_0_url, pkg_1_url];
        mock_pkg_reader.append_update_package(pkg_urls, empty_update_pkg());

        let mock_artifact_reader = MockArtifactReader::new();
        let package_reader: Box<dyn PackageReader> = mock_pkg_reader;
        let artifact_reader: Box<dyn ArtifactReader> = Box::new(mock_artifact_reader);

        PackageDataCollector::collect_with_reader(
            fake_model_config(),
            package_reader,
            artifact_reader,
            Arc::clone(&model),
        )
        .unwrap();

        // Test that the packages are in sorted order.
        let packages = &model.get::<Packages>().unwrap().entries;
        assert_eq!(packages.len(), 2);
        assert_eq!(packages[0].name.to_string(), "bar");
        assert_eq!(packages[1].name.to_string(), "foo");
    }

    #[fuchsia::test]
    fn test_deps() {
        let mut mock_pkg_reader = Box::new(MockPackageReader::new());
        let (_, model) = create_model();

        let merkle_one = Hash::from([1u8; HASH_SIZE]);
        let merkle_two = Hash::from([2u8; HASH_SIZE]);
        let pkg_url_one = AbsolutePackageUrl::parse(&format!(
            "fuchsia-pkg://test.fuchsia.com/one-two-three?hash={}",
            merkle_one.to_string()
        ))
        .unwrap();
        let pkg_url_two = AbsolutePackageUrl::parse(&format!(
            "fuchsia-pkg://test.fuchsia.com/four-five-six?hash={}",
            merkle_two.to_string()
        ))
        .unwrap();

        let mut mock_artifact_reader = Box::new(MockArtifactReader::new());
        mock_pkg_reader.append_pkg_def(PackageDefinition {
            url: pkg_url_one.clone(),
            meta: hashmap! {},
            contents: hashmap! {},
            cms: hashmap! {},
            cvfs: hashmap! {},
        });
        mock_pkg_reader.append_pkg_def(PackageDefinition {
            url: pkg_url_two.clone(),
            meta: hashmap! {},
            contents: hashmap! {},
            cms: hashmap! {},
            cvfs: hashmap! {},
        });

        let pkg_urls = vec![pkg_url_one, pkg_url_two];
        mock_pkg_reader.append_update_package(pkg_urls, empty_update_pkg());

        // Append a dep to both package and artifact readers.
        let pkg_path: PathBuf = "pkg.far".to_string().into();
        let artifact_path: PathBuf = "artifact".to_string().into();
        mock_pkg_reader.append_dep(pkg_path.clone());
        mock_artifact_reader.append_dep(artifact_path.clone());

        PackageDataCollector::collect_with_reader(
            fake_model_config(),
            mock_pkg_reader,
            mock_artifact_reader,
            Arc::clone(&model),
        )
        .unwrap();
        let deps: Arc<CoreDataDeps> = model.get().unwrap();
        assert_eq!(
            deps,
            Arc::new(CoreDataDeps {
                deps: hashset! {
                    pkg_path,
                    artifact_path,
                },
            })
        );
    }

    #[fuchsia::test]
    fn test_extract_config_data_with_only_duplicate_apps() {
        let mock_reader = Box::new(MockPackageReader::new());

        // We will need 2 service package definitions that map different services
        let mut meta = HashMap::new();
        meta.insert(
            PathBuf::from("data/sysmgr/service1.config"),
            create_svc_pkg_bytes(
                vec![],
                vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
            ),
        );
        meta.insert(
            PathBuf::from("data/sysmgr/service2.config"),
            create_svc_pkg_bytes(
                vec![],
                vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
            ),
        );
        let config = fake_model_config();
        let pkg = create_test_package_with_meta(
            config.config_data_package_url().name().clone(),
            Some(PackageVariant::zero()),
            meta,
        );
        let served = vec![pkg];

        let mut package_reader: Box<dyn PackageReader> = mock_reader;
        let result = PackageDataCollector::extract_config_data(
            &config.config_data_package_url(),
            &mut package_reader,
            &served,
        )
        .unwrap();
        assert_eq!(1, result.apps.len());
        assert!(result.apps.contains(
            &AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx").unwrap()
        ));
        assert_eq!(0, result.services.len());
    }

    #[fuchsia::test]
    fn test_extract_config_data_with_only_apps() {
        let mock_reader = Box::new(MockPackageReader::new());

        // We will need 2 service package definitions that map different services
        let mut meta = HashMap::new();
        meta.insert(
            PathBuf::from("data/sysmgr/service1.config"),
            create_svc_pkg_bytes(
                vec![],
                vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
            ),
        );
        meta.insert(
            PathBuf::from("data/sysmgr/service2.config"),
            create_svc_pkg_bytes(
                vec![],
                vec![String::from("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx")],
            ),
        );
        let config = fake_model_config();
        let pkg = create_test_package_with_meta(
            config.config_data_package_url().name().clone(),
            Some(PackageVariant::zero()),
            meta,
        );
        let served = vec![pkg];

        let mut package_reader: Box<dyn PackageReader> = mock_reader;
        let result = PackageDataCollector::extract_config_data(
            &config.config_data_package_url(),
            &mut package_reader,
            &served,
        )
        .unwrap();
        assert_eq!(2, result.apps.len());
        assert!(result.apps.contains(
            &AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx").unwrap()
        ));
        assert!(result.apps.contains(
            &AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx").unwrap()
        ));
        assert_eq!(0, result.services.len());
    }

    #[fuchsia::test]
    fn test_extract_config_data_with_apps_and_services() {
        let mock_reader = Box::new(MockPackageReader::new());

        // We will need 2 service package definitions that map different services
        let mut meta = HashMap::new();
        meta.insert(
            PathBuf::from("data/sysmgr/service1.config"),
            create_svc_pkg_bytes(
                vec![(
                    String::from("fuchsia.test.foo.service1"),
                    String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
                )],
                vec![String::from("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx")],
            ),
        );
        meta.insert(
            PathBuf::from("data/sysmgr/service2.config"),
            create_svc_pkg_bytes(
                vec![(
                    String::from("fuchsia.test.foo.service2"),
                    String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
                )],
                vec![String::from("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx")],
            ),
        );
        let config = fake_model_config();
        let pkg = create_test_package_with_meta(
            config.config_data_package_url().name().clone(),
            Some(PackageVariant::zero()),
            meta,
        );
        let served = vec![pkg];

        let mut package_reader: Box<dyn PackageReader> = mock_reader;
        let result = PackageDataCollector::extract_config_data(
            &config.config_data_package_url(),
            &mut package_reader,
            &served,
        )
        .unwrap();
        assert_eq!(2, result.apps.len());
        assert!(result.apps.contains(
            &AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/bar#meta/bar-app.cmx").unwrap()
        ));
        assert!(result.apps.contains(
            &AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx").unwrap()
        ));

        assert_eq!(2, result.services.len());
        assert!(result.services.contains_key("fuchsia.test.foo.service2"));
        assert_eq!(
            &Url::parse("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx").unwrap(),
            result.services.get("fuchsia.test.foo.service2").unwrap()
        );
        assert!(result.services.contains_key("fuchsia.test.foo.service1"));
        assert_eq!(
            &Url::parse("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx").unwrap(),
            result.services.get("fuchsia.test.foo.service1").unwrap()
        );
    }

    #[fuchsia::test]
    fn test_inferred_service_route_directionality() {
        // Create a single test package with a single unknown service dependency.
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);
        let served = vec![pkg];

        let services = HashMap::new();
        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        // The inferred component provides the service; assert its component id so that we can also
        // be sure that the route source is correct.
        let inferred = response
            .components
            .get(&INFERRED_URL.join("#meta/fuchsia.test.foo.bar.cmx").unwrap())
            .expect("to find inferred route for service");
        assert_eq!(inferred.id, 2);
        // Component 2 is inferred, providing the fuchsia.test.foo.bar service to component 1.
        assert_eq!(
            vec![Route {
                id: 1,
                src_id: 2,
                dst_id: 1,
                service_name: String::from("fuchsia.test.foo.bar"),
                protocol_id: 0,
            }],
            response.routes
        );
    }

    #[fuchsia::test]
    fn test_route_directionality_with_known_services() {
        // Create two test packages, one that depends on a service provided by the other.
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.taurus")]);
        let cms = create_test_cmx_map(vec![(PathBuf::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(PackageName::from_str("foo").unwrap(), None, cms);

        let sb2 = create_test_sandbox(Vec::new());
        let cms2 = create_test_cmx_map(vec![(PathBuf::from("meta/taurus.cmx"), sb2)]);
        let pkg2 =
            create_test_package_with_cms(PackageName::from_str("aries").unwrap(), None, cms2);
        let aries_url = pkg2.url.clone();
        let served = vec![pkg, pkg2];

        let taurus_url = Url::parse(
            &AbsoluteComponentUrl::from_package_url_and_resource(
                aries_url.clone(),
                "meta/taurus.cmx".to_string(),
            )
            .unwrap()
            .to_string(),
        )
        .unwrap();

        // Map the service the first package requires to the second package.
        let mut services = HashMap::new();
        services.insert(String::from("fuchsia.test.taurus"), taurus_url.clone());

        let mut artifact_loader: Box<dyn ArtifactReader> = Box::new(MockArtifactReader::new());
        let response = PackageDataCollector::extract(
            &empty_update_pkg(),
            &mut artifact_loader,
            served,
            services,
            &None,
        )
        .unwrap();

        assert_eq!(2, response.components.len());
        let server = response.components.get(&taurus_url).expect("to find serving component");
        assert_eq!(server.id, 2);
        assert_eq!(
            vec![Route {
                id: 1,
                src_id: 2,
                dst_id: 1,
                service_name: String::from("fuchsia.test.taurus"),
                protocol_id: 0,
            }],
            response.routes
        );
    }

    #[fuchsia::test]
    fn test_missing_images_json_fuchsia_zbi() {
        let zbi_contents = zero_content_zbi();
        let zbi_hash = MerkleTree::from_reader(zbi_contents.as_slice()).unwrap().root();

        // Create valid images.json, but do not add any images to it. In particular, no "fuchsia"
        // image added (which code under test will look for).
        let images_json = ImagePackagesManifest::builder().build();
        let images_json_contents = serde_json::to_vec(&images_json).unwrap();
        let images_json_hash =
            MerkleTree::from_reader(images_json_contents.as_slice()).unwrap().root();

        let update_pkg = create_test_partial_package_with_contents(hashmap! {
            // ZBI is designated in update package as "zbi".
            "zbi".into() => zbi_hash.clone(),
            // Update package contains images.json defined above.
            "images.json".into() => images_json_hash.clone(),
        });

        let mut mock_artifact_reader = MockArtifactReader::new();

        // Code under test will read artifacts in the following order:
        // 1. images.json to determine its ZBI hash;
        mock_artifact_reader.append_artifact(&images_json_hash.to_string(), images_json_contents);
        // 2. the ZBI designated in update package.
        mock_artifact_reader.append_artifact(&zbi_hash.to_string(), zbi_contents);

        let mut artifact_reader: Box<dyn ArtifactReader> = Box::new(mock_artifact_reader);

        // Extraction should succeed because ZBI in update package is sufficent.
        let result = PackageDataCollector::extract_zbi_from_update_package(
            &mut artifact_reader,
            &update_pkg,
            &vec![],
        );
        match result {
            Ok(_) => return,
            Err(err) => panic!("Unexpected error: {:?}", err),
        };
    }

    #[fuchsia::test]
    fn test_missing_update_package_fuchsia_zbi() {
        let zbi_contents = zero_content_zbi();
        let zbi_content_hash = content_hash(zbi_contents.as_slice());
        let zbi_hash = MerkleTree::from_reader(zbi_contents.as_slice()).unwrap().root();

        // Create valid images.json with images that includes a ZBI.
        let mut images_json_builder = ImagePackagesManifest::builder();
        let url = "fuchsia-pkg://test.fuchsia.com/update-images-fuchsia/0?hash=0000000000000000000000000000000000000000000000000000000000000000#data".parse().unwrap();

        images_json_builder.fuchsia_package(
            ImageMetadata::new(zbi_contents.len().try_into().unwrap(), zbi_content_hash, url),
            None,
        );
        let images_json = images_json_builder.build();
        let images_json_contents = serde_json::to_vec(&images_json).unwrap();
        let images_json_hash =
            MerkleTree::from_reader(images_json_contents.as_slice()).unwrap().root();

        let update_pkg = create_test_partial_package_with_contents(
            // No ZBI designated in update package (either as "zbi" or "zbi.signed").
            hashmap! {
                // Update package contains images.json defined above.
                "images.json".into() => images_json_hash.clone(),
            },
        );
        let images_pkg = create_test_package_with_contents(
            PackageName::from_str("update-images-fuchsia").unwrap(),
            Some(PackageVariant::zero()),
            // Designate a ZBI in images package.
            hashmap! {
                "zbi".into() => zbi_hash.clone(),
            },
        );

        let mut mock_artifact_reader = MockArtifactReader::new();

        // Code under test will read artifacts in the following order:
        // 1. images.json to determine its ZBI hash;
        mock_artifact_reader.append_artifact(&images_json_hash.to_string(), images_json_contents);
        // 2. the ZBI designated in images.json.
        mock_artifact_reader.append_artifact(&zbi_hash.to_string(), zbi_contents);

        let mut artifact_reader: Box<dyn ArtifactReader> = Box::new(mock_artifact_reader);

        // Extraction should succeed because ZBI in images.json is sufficent.
        let result = PackageDataCollector::extract_zbi_from_update_package(
            &mut artifact_reader,
            &update_pkg,
            &vec![images_pkg],
        );

        match result {
            Ok(_) => return,
            Err(err) => panic!("Unexpected error: {:?}", err),
        };
    }

    #[fuchsia::test]
    fn test_update_package_vs_images_json_zbi_hash_mismatch() {
        let zbi_contents = zero_content_zbi();
        let zbi_content_hash = content_hash(zbi_contents.as_slice());
        let zbi_hash = MerkleTree::from_reader(zbi_contents.as_slice()).unwrap().root();
        let zbi_mismatch_hash = Hash::from([9; HASH_SIZE]);
        assert!(zbi_hash != zbi_mismatch_hash);

        // Create valid images.json with "fuchsia" images that includes a ZBI.
        let mut images_json_builder = ImagePackagesManifest::builder();
        let url = "fuchsia-pkg://test.fuchsia.com/update-images-fuchsia/0?hash=0000000000000000000000000000000000000000000000000000000000000000#data".parse().unwrap();

        images_json_builder.fuchsia_package(
            ImageMetadata::new(zbi_contents.len().try_into().unwrap(), zbi_content_hash, url),
            None,
        );

        let images_json = images_json_builder.build();
        let images_json_contents = serde_json::to_vec(&images_json).unwrap();
        let images_json_hash =
            MerkleTree::from_reader(images_json_contents.as_slice()).unwrap().root();

        let update_pkg = create_test_partial_package_with_contents(
            // ZBI is designated in update package as "zbi". Note that fake hash string,
            // "zbi_hash_from_update_package" will not match string for hash
            // "zbi_hash_from_images_package" designated by `images_pkg` below.
            hashmap! {
                "zbi".into() => zbi_hash,
                // Update package contains images.json defined above.
                "images.json".into() => images_json_hash.clone(),
            },
        );
        let images_pkg = create_test_package_with_contents(
            PackageName::from_str("update-images-fuchsia").unwrap(),
            Some(PackageVariant::zero()),
            // Designate a ZBI in images package.
            hashmap! {
                "zbi".into() => zbi_mismatch_hash,
            },
        );

        let mut mock_artifact_reader = MockArtifactReader::new();

        // Code under test will read one artifact: images.json.
        mock_artifact_reader.append_artifact(
            &images_json_hash.to_string(),
            serde_json::to_vec(&images_json).unwrap(),
        );

        let mut artifact_reader: Box<dyn ArtifactReader> = Box::new(mock_artifact_reader);
        assert!(PackageDataCollector::extract_zbi_from_update_package(
            &mut artifact_reader,
            &update_pkg,
            &vec![images_pkg],
        )
        .err()
        .unwrap()
        .to_string()
        .contains("Update package and its images manifest contain different fuchsia ZBI images"));
    }

    #[fuchsia::test]
    fn test_update_package_vs_images_json_zbi_hash_match() {
        let zbi_contents = zero_content_zbi();
        let zbi_content_hash = content_hash(zbi_contents.as_slice());
        let zbi_hash = MerkleTree::from_reader(zbi_contents.as_slice()).unwrap().root();

        // Create valid images.json with "fuchsia" images that includes a ZBI.
        let mut images_json_builder = ImagePackagesManifest::builder();
        let url = "fuchsia-pkg://fuchsia.com/update-images-firmware/0?hash=000000000000000000000000000000000000000000000000000000000000000a#data".parse().unwrap();

        images_json_builder.fuchsia_package(
            ImageMetadata::new(zbi_contents.len().try_into().unwrap(), zbi_content_hash, url),
            None,
        );
        let images_json = images_json_builder.build();
        let images_json_contents = serde_json::to_vec(&images_json).unwrap();
        let images_json_hash =
            MerkleTree::from_reader(images_json_contents.as_slice()).unwrap().root();

        let update_pkg = create_test_partial_package_with_contents(hashmap! {
            // ZBI is designated in update package as "zbi". Hash string value matches hash in
            // `images_json` above.
            "zbi".into() => zbi_hash.clone(),
            // Update package contains images.json defined above.
            "images.json".into() => images_json_hash.clone(),
        });
        let images_pkg = create_test_package_with_contents(
            PackageName::from_str("update-images-fuchsia").unwrap(),
            Some(PackageVariant::zero()),
            // Designate a ZBI in images package.
            hashmap! {
                "zbi".into() => zbi_hash.clone(),
            },
        );

        let mut mock_artifact_reader = MockArtifactReader::new();

        // Code under test will read artifacts in the following order:
        // 1. images.json to determine its ZBI hash;
        mock_artifact_reader.append_artifact(&images_json_hash.to_string(), images_json_contents);
        // 2. the ZBI designated by both the update package and image.json.
        mock_artifact_reader.append_artifact(&zbi_hash.to_string(), zbi_contents);

        let mut artifact_reader: Box<dyn ArtifactReader> = Box::new(mock_artifact_reader);
        let result = PackageDataCollector::extract_zbi_from_update_package(
            &mut artifact_reader,
            &update_pkg,
            &vec![images_pkg],
        );
        match result {
            Ok(_) => return,
            Err(err) => panic!("Unexpected error: {:?}", err),
        };
    }
}
