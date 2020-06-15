// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        engine::hook::PluginHooks,
        engine::plugin::{Plugin, PluginDescriptor},
        model::collector::DataCollector,
        model::model::DataModel,
        plugins::components::{http::HttpGetter, package_reader::*, types::*, util},
    },
    anyhow::{anyhow, Result},
    futures_executor::block_on,
    lazy_static::lazy_static,
    log::{info, warn},
    regex::Regex,
    std::collections::HashMap,
    std::env,
    std::str,
    std::sync::Arc,
};

// Constants/Statics
lazy_static! {
    static ref SERVICE_CONFIG_RE: Regex = Regex::new(r"data/sysmgr/.+\.config").unwrap();
}
pub const CONFIG_DATA_PKG_URL: &str = "fuchsia-pkg://fuchsia.com/config-data";

/* ---- TAKEN FROM CL 389350 ---- */

/// Defines a component. Each component has a unique id which is used to link
/// it in the Route table. Each component also has a url and a version. This
/// structure is intended to be lightweight and general purpose if you need to
/// append additional information about a component make another table and
/// index it on the `component.id`.
pub struct Component {
    pub id: i32,
    pub url: String,
    pub version: i32,
    // Added fields below
    pub inferred: bool,
    // End added fields
}

/// Defines a component manifest. The `component_id` maps 1:1 to
/// `component.id` indexes. This is stored in a different table as most queries
/// don't need the raw manifest.
pub struct Manifest {
    pub component_id: i32,
    pub manifest: String,
    // Added fields below
    pub uses: Vec<String>,
    // End added fields
}

/// Defines a link between two components. The `src_id` is the `component.id`
/// of the component giving a service or directory to the `dst_id`. The
/// `protocol_id` refers to the Protocol with this link.
pub struct Route {
    pub id: i32,
    pub src_id: i32,
    pub dst_id: i32,
    pub protocol_id: i32,
}

/// Defines either a FIDL or Directory protocol with some interface name such
/// as fuchshia.foo.Bar and an optional path such as "/dev".
pub struct Protocol {
    pub id: i32,
    pub interface: String,
    pub path: String,
}

/* ---- END COPY ---- */

// TODO: make this impl the DataCollector trait and actually do the collection
// and hand off to model creation.
pub struct PackageDataCollector {
    package_reader: Box<dyn PackageReader>,
}

impl PackageDataCollector {
    async fn get_packages(&self) -> Result<Vec<PackageDefinition>> {
        // Retrieve the json packages definition from the package server.
        let targets = self.package_reader.read_targets().await?;

        let mut pkgs: Vec<PackageDefinition> = Vec::new();
        for (name, target) in targets.signed.targets.iter() {
            let pkg_def =
                self.package_reader.read_package_definition(&name, &target.custom.merkle).await?;
            pkgs.push(pkg_def);
        }

        Ok(pkgs)
    }

    async fn get_builtins(&self) -> Result<(Vec<PackageDefinition>, ServiceMapping)> {
        let builtins = self.package_reader.read_builtins().await?;

        let mut packages = Vec::new();
        for package in builtins.packages {
            let mut pkg_def = PackageDefinition {
                url: util::to_package_url(&package.url)?,
                typ: PackageType::BUILTIN,
                merkle: String::from("0"),
                contents: HashMap::new(),
                cms: HashMap::new(),
            };

            pkg_def.cms.insert(String::from(""), CmxDefinition::from(package.manifest));
            packages.push(pkg_def);
        }

        Ok((packages, builtins.services))
    }

    fn new() -> Result<Self> {
        let fuchsia_dir = env::var("FUCHSIA_DIR")?;
        if fuchsia_dir.len() == 0 {
            return Err(anyhow!("Unable to retrieve $FUCHSIA_DIR, has it been set?"));
        }

        Ok(Self {
            package_reader: Box::new(PackageServerReader::new(
                fuchsia_dir,
                Box::new(HttpGetter::new(String::from("127.0.0.1:8083"))),
            )),
        })
    }

    /// Collects and builds a DAG of component nodes (with manifests) and routes that
    /// connect the nodes.
    async fn collect_manifests(&self) -> Result<()> {
        let served_packages = self.get_packages().await?;

        let (builtin_packages, builtin_services) = self.get_builtins().await?;

        let services = self.merge_services(&served_packages, builtin_services).await?;

        info!(
            "Done collecting. Found {} services, {} served packages, and {} builtin packages.",
            services.keys().len(),
            served_packages.len(),
            builtin_packages.len()
        );

        // TODO: Push to data model
        PackageDataCollector::build_model(served_packages, builtin_packages, services).await?;
        Ok(())
    }

    // Combine service name->url mappings from builtins and those defined in config-data.
    // This consumes the builtins ServiceMapping as part of building the combined map.
    async fn merge_services(
        &self,
        served: &Vec<PackageDefinition>,
        builtins: ServiceMapping,
    ) -> Result<ServiceMapping> {
        let mut combined = ServiceMapping::new();

        // Find and add all services as defined in config-data
        for pkg_def in served {
            if pkg_def.url == CONFIG_DATA_PKG_URL {
                for (name, merkle) in &pkg_def.contents {
                    if SERVICE_CONFIG_RE.is_match(&name) {
                        let service_pkg =
                            self.package_reader.read_service_package_definition(merkle).await?;

                        if let Some(services) = service_pkg.services {
                            for (service_name, service_url) in services {
                                if combined.contains_key(&service_name) {
                                    warn!(
                                        "Service mapping collision on {} between {} and {}",
                                        service_name, combined[&service_name], service_url
                                    );
                                }

                                combined.insert(service_name, service_url);
                            }
                        } else {
                            warn!("Expected service with merkle {} to exist. Optimistically continuing.", merkle);
                        }
                    }
                }
            }
        }
        // Add all builtin services
        for (service_name, service_url) in builtins {
            if combined.contains_key(&service_name) {
                warn!(
                    "Service mapping collision on {} between {} and {}",
                    service_name, combined[&service_name], service_url
                );
            }

            combined.insert(service_name, service_url);
        }

        Ok(combined)
    }

    /// Function to build the component graph model out of the packages and services retrieved
    /// by this collector.
    /// TODO: Move this to a DataModel.
    async fn build_model(
        served_pkgs: Vec<PackageDefinition>,
        _builtin_pkgs: Vec<PackageDefinition>,
        mut service_map: ServiceMapping,
    ) -> Result<(HashMap<String, Component>, Vec<Manifest>, Vec<Route>)> {
        let mut components: HashMap<String, Component> = HashMap::new();
        let mut manifests: Vec<Manifest> = Vec::new();
        let mut routes: Vec<Route> = Vec::new();

        // Iterate through all served packages, for each cmx they define, create a node.
        let mut idx = 0; // FIXME: How to generate?
        for pkg in served_pkgs.iter() {
            for (path, cm) in &pkg.cms {
                if path.starts_with("meta/") && path.ends_with(".cmx") {
                    idx += 1;
                    let url = format!("{}#{}", pkg.url, path);
                    components.insert(
                        url.clone(),
                        Component {
                            id: idx,
                            url: url,
                            version: 0, // FIXME: Is this the version of the cmx? It doesn't seem to be defined anywhere.
                            inferred: false,
                        },
                    );

                    let mani = {
                        if let Some(sandbox) = &cm.sandbox {
                            Manifest {
                                component_id: idx,
                                manifest: serde_json::to_string(&sandbox)?,
                                uses: {
                                    match sandbox.services.as_ref() {
                                        Some(svcs) => svcs.clone(),
                                        None => Vec::new(),
                                    }
                                },
                            }
                        } else {
                            Manifest {
                                component_id: idx,
                                manifest: String::from(""),
                                uses: Vec::new(),
                            }
                        }
                    };
                    manifests.push(mani);
                }
            }
        }

        // Iterate through all services mappings, for each one, find the associated node or create a new
        // inferred node and mark it as a provider of that service.
        for (service_name, pkg_url) in service_map.iter() {
            if !components.contains_key(pkg_url) {
                // We don't already know about the component that *should* provide this service.
                // Create an inferred node.
                warn!("Expected component {} exist to provide service {} but it does not exist. Creating inferred node.", pkg_url, service_name);
                idx += 1;
                components.insert(
                    pkg_url.clone(),
                    Component {
                        id: idx,
                        url: pkg_url.clone(),
                        version: 0, // FIXME:
                        inferred: true,
                    },
                );
            }
        }

        // Iterate through all nodes created thus far, creating edges between them based on the services they use.
        // If a service provider node is not able to be found, create a new inferred service provider node.
        // Since manifests more naturally hold the list of services that the component requires, we iterate through
        // those instead. Can be changed relatively effortlessly if the model make sense otherwise.
        let mut route_idx = 0; // FIXME:
        for mani in &manifests {
            for service_name in &mani.uses {
                let target_id = {
                    if service_map.contains_key(service_name) {
                        // FIXME: Options do not impl Try so we cannot ? but there must be some better way to get at a value...
                        components.get(service_map.get(service_name).unwrap()).unwrap().id
                    } else {
                        // Even the service map didn't know about this service. We should create an inferred component
                        // that provides this service.
                        warn!("Expected a service provider for service {} but it does not exist. Creating inferred node.", service_name);
                        idx += 1;
                        let url = format!("fuchsia-pkg://inferred#meta/{}.cmx", service_name);
                        components.insert(
                            url.clone(),
                            Component {
                                id: idx,
                                url: url.clone(),
                                version: 0, // FIXME:
                                inferred: true,
                            },
                        );
                        // Add the inferred node to the service map to be found by future consumers of the service
                        service_map.insert(String::from(service_name), url);
                        idx
                    }
                };
                route_idx += 1;
                routes.push(Route {
                    id: route_idx,
                    src_id: mani.component_id,
                    dst_id: target_id,
                    protocol_id: 0, // FIXME:
                });
            }
        }

        info!(
            "Components: {}, Manifests {}, Routes {}.",
            components.len(),
            manifests.len(),
            routes.len()
        );

        Ok((components, manifests, routes))
    }
}

impl DataCollector for PackageDataCollector {
    fn collect(&self, _: Arc<DataModel>) -> Result<()> {
        block_on(self.collect_manifests())
    }
}

plugin!(
    ComponentGraphPlugin,
    PluginHooks::new(vec![Arc::new(PackageDataCollector::new().unwrap())], controller_hooks! {}),
    vec![]
);

// It doesn't seem incredibly useful to test the json parsing or the far file
// parsing, as those are mainly provided by dependencies.
// The retireval from package server is defined here, but feels difficult to
// test in a meaningful way.
// As a result, the bulk of the testing is focused around the graph/model
// building logic.
#[cfg(test)]
mod tests {
    use {
        super::*, crate::plugins::components::jsons::*, async_trait::async_trait,
        std::cell::RefCell,
    };

    struct MockPackageReader {
        targets: RefCell<Vec<TargetsJson>>,
        package_defs: RefCell<Vec<PackageDefinition>>,
        service_package_defs: RefCell<Vec<ServicePackageDefinition>>,
        builtins: RefCell<Vec<BuiltinsJson>>,
    }

    impl MockPackageReader {
        fn new() -> Self {
            Self {
                targets: RefCell::new(Vec::new()),
                package_defs: RefCell::new(Vec::new()),
                service_package_defs: RefCell::new(Vec::new()),
                builtins: RefCell::new(Vec::new()),
            }
        }

        // Adds values to the FIFO queue for targets
        fn _append_target(&self, target: TargetsJson) {
            self.targets.borrow_mut().push(target);
        }
        // Adds values to the FIFO queue for package_defs
        fn _append_pkg_def(&self, package_def: PackageDefinition) {
            self.package_defs.borrow_mut().push(package_def);
        }
        // Adds values to the FIFO queue for service_package_defs
        fn append_service_pkg_def(&self, svc_pkg_def: ServicePackageDefinition) {
            self.service_package_defs.borrow_mut().push(svc_pkg_def);
        }
        // Adds values to the FIFO queue for builtins
        fn _append_builtin(&self, builtin: BuiltinsJson) {
            self.builtins.borrow_mut().push(builtin);
        }
    }

    #[async_trait(?Send)]
    impl PackageReader for MockPackageReader {
        async fn read_targets(&self) -> Result<TargetsJson> {
            let mut borrow = self.targets.borrow_mut();
            {
                if borrow.len() == 0 {
                    return Err(anyhow!("No more targets left to return. Maybe append more?"));
                }
                Ok(borrow.remove(0))
            }
        }
        async fn read_package_definition(
            &self,
            _pkg_name: &str,
            _merkle: &str,
        ) -> Result<PackageDefinition> {
            let mut borrow = self.package_defs.borrow_mut();
            {
                if borrow.len() == 0 {
                    return Err(anyhow!("No more package_defs left to return. Maybe append more?"));
                }
                Ok(borrow.remove(0))
            }
        }

        async fn read_service_package_definition(
            &self,
            _merkle: &str,
        ) -> Result<ServicePackageDefinition> {
            let mut borrow = self.service_package_defs.borrow_mut();
            {
                if borrow.len() == 0 {
                    return Err(anyhow!(
                        "No more service_package_defs left to return. Maybe append more?"
                    ));
                }
                Ok(borrow.remove(0))
            }
        }

        async fn read_builtins(&self) -> Result<BuiltinsJson> {
            let mut borrow = self.builtins.borrow_mut();
            {
                if borrow.len() == 0 {
                    return Err(anyhow!("No more builtins left to return. Maybe append more?"));
                }
                Ok(borrow.remove(0))
            }
        }
    }

    fn create_test_sandbox(uses: Vec<String>) -> SandboxDefinition {
        SandboxDefinition {
            dev: None,
            services: Some(uses),
            system: None,
            pkgfs: None,
            features: None,
        }
    }

    fn create_test_cmx_map(
        entries: Vec<(String, SandboxDefinition)>,
    ) -> HashMap<String, CmxDefinition> {
        entries
            .into_iter()
            .map(|entry| (entry.0, CmxDefinition { sandbox: Some(entry.1) }))
            .collect()
    }

    fn create_test_package_with_cms(
        url: String,
        cms: HashMap<String, CmxDefinition>,
    ) -> PackageDefinition {
        PackageDefinition {
            url: url,
            merkle: String::from("0"),
            typ: PackageType::PACKAGE,
            contents: HashMap::new(),
            cms: cms,
        }
    }

    fn create_test_package_with_contents(
        url: String,
        contents: HashMap<String, String>,
    ) -> PackageDefinition {
        PackageDefinition {
            url: url,
            merkle: String::from("0"),
            typ: PackageType::PACKAGE,
            contents: contents,
            cms: HashMap::new(),
        }
    }

    fn create_svc_pkg_def(entries: Vec<(String, String)>) -> ServicePackageDefinition {
        ServicePackageDefinition {
            services: Some(entries.into_iter().map(|entry| (entry.0, entry.1)).collect()),
        }
    }

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

    // =-=-=-=-= merge_services() tests =-=-=-=-= //

    #[test]
    fn test_merge_services_ignores_services_defined_on_non_config_data_package() {
        // Create a single package that is NOT the config data package
        let mock_reader = MockPackageReader::new();
        let collector = PackageDataCollector { package_reader: Box::new(mock_reader) };

        let mut contents = HashMap::new();
        contents.insert(String::from("data/sysmgr/foo.config"), String::from("test_merkle"));
        let pkg = create_test_package_with_contents(
            String::from("fuchsia-pkg://fuchsia.com/not-config-data"),
            contents,
        );
        let served = vec![pkg];
        let builtins = HashMap::new();

        let result = block_on(collector.merge_services(&served, builtins)).unwrap();
        assert_eq!(0, result.len());
    }

    #[test]
    fn test_merge_services_ignores_services_defined_by_non_config_meta_contents() {
        // Create a single package that IS the config data package but
        // does not contain valid data/sysmgr/*.config meta content.
        let mock_reader = MockPackageReader::new();
        let collector = PackageDataCollector { package_reader: Box::new(mock_reader) };

        let mut contents = HashMap::new();
        contents.insert(String::from("not/valid/config"), String::from("test_merkle"));
        let pkg = create_test_package_with_contents(String::from(CONFIG_DATA_PKG_URL), contents);
        let served = vec![pkg];
        let builtins = HashMap::new();

        let result = block_on(collector.merge_services(&served, builtins)).unwrap();
        assert_eq!(0, result.len());
    }

    #[test]
    fn test_merge_services_takes_the_last_defined_duplicate_config_data_services() {
        let mock_reader = MockPackageReader::new();
        // We will need 2 service package definitions that map the same service to different components
        mock_reader.append_service_pkg_def(create_svc_pkg_def(vec![(
            String::from("fuchsia.test.foo.bar"),
            String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
        )]));
        mock_reader.append_service_pkg_def(create_svc_pkg_def(vec![(
            String::from("fuchsia.test.foo.bar"),
            String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
        )]));

        let collector = PackageDataCollector { package_reader: Box::new(mock_reader) };

        let mut contents = HashMap::new();
        contents.insert(String::from("data/sysmgr/foo.config"), String::from("test_merkle"));
        contents.insert(String::from("data/sysmgr/bar.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_contents(String::from(CONFIG_DATA_PKG_URL), contents);
        let served = vec![pkg];
        let builtins = HashMap::new();

        let result = block_on(collector.merge_services(&served, builtins)).unwrap();
        assert_eq!(1, result.len());
        assert!(result.contains_key("fuchsia.test.foo.bar"));
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx",
            result.get("fuchsia.test.foo.bar").unwrap()
        );
    }

    #[test]
    fn test_merge_services_prioritizes_builtins_defined_duplicate_service() {
        let mock_reader = MockPackageReader::new();
        // We will need 1 service package definition
        mock_reader.append_service_pkg_def(create_svc_pkg_def(vec![(
            String::from("fuchsia.test.foo.bar"),
            String::from("fuchsia-pkg://fuchsia.com/foo#meta/served.cmx"),
        )]));

        let collector = PackageDataCollector { package_reader: Box::new(mock_reader) };

        let mut contents = HashMap::new();
        contents.insert(String::from("data/sysmgr/foo.config"), String::from("test_merkle"));
        let pkg = create_test_package_with_contents(String::from(CONFIG_DATA_PKG_URL), contents);
        let served = vec![pkg];

        // We need 1 builtin service package definition that matches the service name
        // created above, but with a different component
        let mut builtins = HashMap::new();
        builtins.insert(
            String::from("fuchsia.test.foo.bar"),
            String::from("fuchsia-pkg://fuchsia.com/foo#meta/builtin.cmx"),
        );

        let result = block_on(collector.merge_services(&served, builtins)).unwrap();
        assert_eq!(1, result.len());
        assert!(result.contains_key("fuchsia.test.foo.bar"));
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/foo#meta/builtin.cmx",
            result.get("fuchsia.test.foo.bar").unwrap()
        );
    }

    #[test]
    fn test_merge_services_merges_unique_service_names() {
        let mock_reader = MockPackageReader::new();
        // We will need 2 service package definitions that map different services
        mock_reader.append_service_pkg_def(create_svc_pkg_def(vec![(
            String::from("fuchsia.test.foo.service1"),
            String::from("fuchsia-pkg://fuchsia.com/foo#meta/served1.cmx"),
        )]));
        mock_reader.append_service_pkg_def(create_svc_pkg_def(vec![(
            String::from("fuchsia.test.foo.service2"),
            String::from("fuchsia-pkg://fuchsia.com/foo#meta/served2.cmx"),
        )]));

        let collector = PackageDataCollector { package_reader: Box::new(mock_reader) };

        let mut contents = HashMap::new();
        contents.insert(String::from("data/sysmgr/service1.config"), String::from("test_merkle"));
        contents.insert(String::from("data/sysmgr/service2.config"), String::from("test_merkle_2"));
        let pkg = create_test_package_with_contents(String::from(CONFIG_DATA_PKG_URL), contents);
        let served = vec![pkg];

        // Create 1 builtin service that is unique to those above
        let mut builtins = HashMap::new();
        builtins.insert(
            String::from("fuchsia.test.foo.service3"),
            String::from("fuchsia-pkg://fuchsia.com/foo#meta/builtin.cmx"),
        );

        let result = block_on(collector.merge_services(&served, builtins)).unwrap();
        assert_eq!(3, result.len());
    }

    // =-=-=-=-= build_model() tests =-=-=-=-= //

    #[test]
    fn test_build_model_with_no_services_infers_service() {
        // Create a single test package with a single unknown service dependency
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        let served = vec![pkg];

        let builtins = Vec::new();
        let services = HashMap::new();

        let (components, manifests, routes) =
            block_on(PackageDataCollector::build_model(served, builtins, services)).unwrap();

        assert_eq!(2, components.len());
        assert_eq!(1, manifests.len());
        assert_eq!(1, routes.len());
        assert_eq!((1, 1), count_defined_inferred(components)); // 1 real, 1 inferred
    }

    #[test]
    fn test_build_model_with_known_services_but_no_matching_component_infers_component() {
        // Create a single test package with a single known service dependency
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/baz.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        let served = vec![pkg];

        let builtins = Vec::new();
        // We know about the desired service in the service mapping, but the component doesn't exist
        let mut services = HashMap::new();
        services.insert(
            String::from("fuchsia.test.foo.bar"),
            String::from("fuchsia-pkg://fuchsia.com/aries#meta/taurus.cmx"),
        );

        let (components, manifests, routes) =
            block_on(PackageDataCollector::build_model(served, builtins, services)).unwrap();

        assert_eq!(2, components.len());
        assert_eq!(1, manifests.len());
        assert_eq!(1, routes.len());
        assert_eq!((1, 1), count_defined_inferred(components)); // 1 real, 1 inferred
    }

    #[test]
    fn test_build_model_with_invalid_cmx_creates_empty_graph() {
        // Create a single test package with an invalid cmx path
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.foo.bar")]);
        let sb2 = create_test_sandbox(vec![String::from("fuchsia.test.foo.baz")]);
        let cms = create_test_cmx_map(vec![
            (String::from("foo/bar.cmx"), sb),
            (String::from("meta/baz"), sb2),
        ]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);
        let served = vec![pkg];

        let builtins = Vec::new();
        let services = HashMap::new();

        let (components, manifests, routes) =
            block_on(PackageDataCollector::build_model(served, builtins, services)).unwrap();

        assert_eq!(0, components.len());
        assert_eq!(0, manifests.len());
        assert_eq!(0, routes.len());
        assert_eq!((0, 0), count_defined_inferred(components)); // 0 real, 0 inferred
    }

    #[test]
    fn test_build_model_with_duplicate_inferred_services_reuses_inferred_service() {
        // Create two test packages that depend on the same inferred service
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);

        let sb2 = create_test_sandbox(vec![String::from("fuchsia.test.service")]);
        let cms2 = create_test_cmx_map(vec![(String::from("meta/taurus.cmx"), sb2)]);
        let pkg2 =
            create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/aries"), cms2);
        let served = vec![pkg, pkg2];

        let builtins = Vec::new();
        let services = HashMap::new();

        let (components, manifests, routes) =
            block_on(PackageDataCollector::build_model(served, builtins, services)).unwrap();

        assert_eq!(3, components.len());
        assert_eq!(2, manifests.len());
        assert_eq!(2, routes.len());
        assert_eq!((2, 1), count_defined_inferred(components)); // 2 real, 1 inferred
    }

    #[test]
    fn test_build_model_with_known_services_does_not_infer_service() {
        // Create two test packages, one that depends on a service provided by the other
        let sb = create_test_sandbox(vec![String::from("fuchsia.test.taurus")]);
        let cms = create_test_cmx_map(vec![(String::from("meta/bar.cmx"), sb)]);
        let pkg = create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/foo"), cms);

        let sb2 = create_test_sandbox(Vec::new());
        let cms2 = create_test_cmx_map(vec![(String::from("meta/taurus.cmx"), sb2)]);
        let pkg2 =
            create_test_package_with_cms(String::from("fuchsia-pkg://fuchsia.com/aries"), cms2);
        let served = vec![pkg, pkg2];

        let builtins = Vec::new();
        // Map the service the first package requires to the second package
        let mut services = HashMap::new();
        services.insert(
            String::from("fuchsia.test.taurus"),
            String::from("fuchsia-pkg://fuchsia.com/aries#meta/taurus.cmx"),
        );

        let (components, manifests, routes) =
            block_on(PackageDataCollector::build_model(served, builtins, services)).unwrap();

        assert_eq!(2, components.len());
        assert_eq!(2, manifests.len());
        assert_eq!(1, routes.len());
        assert_eq!((2, 0), count_defined_inferred(components)); // 2 real, 0 inferred
    }
}
