// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    assert_matches::assert_matches,
    async_trait::async_trait,
    cm_fidl_analyzer::{
        component_instance::ComponentInstanceForAnalyzer,
        component_model::{AnalyzerModelError, ComponentModelForAnalyzer, ModelBuilderForAnalyzer},
        environment::{BOOT_RESOLVER_NAME, BOOT_SCHEME},
        node_path::NodePath,
        route::{CapabilityRouteError, RouteSegment, VerifyRouteResult},
    },
    cm_rust::{
        Availability, CapabilityDecl, CapabilityName, CapabilityPath, CapabilityTypeName, ChildRef,
        ComponentDecl, DependencyType, ExposeDecl, ExposeDeclCommon, ExposeDirectoryDecl,
        ExposeProtocolDecl, ExposeResolverDecl, ExposeServiceDecl, ExposeSource, ExposeTarget,
        OfferDecl, OfferDirectoryDecl, OfferEventDecl, OfferProtocolDecl, OfferServiceDecl,
        OfferSource, OfferStorageDecl, OfferTarget, ProtocolDecl, RegistrationSource, ResolverDecl,
        ResolverRegistration, RunnerDecl, RunnerRegistration, ServiceDecl, StorageDecl,
        StorageDirectorySource, UseDecl, UseDirectoryDecl, UseEventDecl, UseEventStreamDecl,
        UseProtocolDecl, UseServiceDecl, UseSource, UseStorageDecl,
    },
    cm_rust_testing::{
        ChildDeclBuilder, ComponentDeclBuilder, DirectoryDeclBuilder, EnvironmentDeclBuilder,
        ProtocolDeclBuilder,
    },
    fidl::prelude::*,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_internal as component_internal,
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon_status as zx_status,
    futures::FutureExt,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase},
    routing::{
        component_id_index::ComponentIdIndex,
        component_instance::ComponentInstanceInterface,
        config::{
            AllowlistEntry, CapabilityAllowlistKey, DebugCapabilityAllowlistEntry,
            DebugCapabilityKey, RuntimeConfig, SecurityPolicy,
        },
        environment::RunnerRegistry,
        error::RoutingError,
        rights::{READ_RIGHTS, WRITE_RIGHTS},
        RegistrationDecl,
    },
    routing_test_helpers::{
        CheckUse, ComponentEventRoute, ExpectedResult, RoutingTestModel, RoutingTestModelBuilder,
    },
    std::{
        collections::{HashMap, HashSet},
        convert::{TryFrom, TryInto},
        iter::FromIterator,
        path::Path,
        sync::Arc,
    },
    thiserror::Error,
    url::Url,
};

const TEST_URL_PREFIX: &str = "test:///";
// Placeholder for when a component resolves to itself, and its name is unknown as a result.
const USE_TARGET_PLACEHOLDER_NAME: &str = "target";

fn make_test_url(component_name: &str) -> String {
    format!("{}{}", TEST_URL_PREFIX, component_name)
}

pub struct RoutingTestForAnalyzer {
    model: Arc<ComponentModelForAnalyzer>,
}

pub struct RoutingTestBuilderForAnalyzer {
    root_url: cm_types::Url,
    decls_by_url: HashMap<url::Url, (ComponentDecl, Option<config_encoder::ConfigFields>)>,
    namespace_capabilities: Vec<CapabilityDecl>,
    builtin_capabilities: Vec<CapabilityDecl>,
    builtin_runner_registrations: Vec<RunnerRegistration>,
    capability_policy: HashMap<CapabilityAllowlistKey, HashSet<AllowlistEntry>>,
    debug_capability_policy: HashMap<DebugCapabilityKey, HashSet<DebugCapabilityAllowlistEntry>>,
    component_id_index_path: Option<String>,
    builtin_boot_resolver: component_internal::BuiltinBootResolver,
}

impl RoutingTestBuilderForAnalyzer {
    fn set_builtin_boot_resolver(&mut self, resolver: component_internal::BuiltinBootResolver) {
        self.builtin_boot_resolver = resolver;
    }

    // Creates a new builder with the specified map of component URLs to `ComponentDecl`s, rather
    // than using the default test URL scheme.
    fn new_with_custom_urls(root_url: String, components: Vec<(String, ComponentDecl)>) -> Self {
        let root_url = cm_types::Url::new(root_url).expect("failed to parse root component url");
        let decls_by_url =
            HashMap::from_iter(components.into_iter().map(|(url_string, component_decl)| {
                (Url::parse(&url_string).unwrap(), (component_decl, None))
            }));
        Self {
            root_url,
            decls_by_url,
            namespace_capabilities: Vec::new(),
            builtin_capabilities: Vec::new(),
            builtin_runner_registrations: Vec::new(),
            capability_policy: HashMap::new(),
            debug_capability_policy: HashMap::new(),
            component_id_index_path: None,
            builtin_boot_resolver: component_internal::BuiltinBootResolver::None,
        }
    }
}

#[async_trait]
impl RoutingTestModelBuilder for RoutingTestBuilderForAnalyzer {
    type Model = RoutingTestForAnalyzer;

    // Creates a new builder with the specified components. Components are specified by name;
    // the method assigns each component a test URL.
    fn new(root_component: &str, components: Vec<(&'static str, ComponentDecl)>) -> Self {
        let root_url = cm_types::Url::new(make_test_url(root_component))
            .expect("failed to parse root component url");
        let decls_by_url = HashMap::from_iter(components.into_iter().map(|(name, decl)| {
            (Url::parse(&format!("{}{}", TEST_URL_PREFIX, name)).unwrap(), (decl, None))
        }));
        Self {
            root_url,
            decls_by_url,
            namespace_capabilities: Vec::new(),
            builtin_capabilities: Vec::new(),
            builtin_runner_registrations: Vec::new(),
            capability_policy: HashMap::new(),
            debug_capability_policy: HashMap::new(),
            component_id_index_path: None,
            builtin_boot_resolver: component_internal::BuiltinBootResolver::None,
        }
    }

    fn set_namespace_capabilities(&mut self, caps: Vec<CapabilityDecl>) {
        self.namespace_capabilities = caps;
    }

    fn set_builtin_capabilities(&mut self, caps: Vec<CapabilityDecl>) {
        self.builtin_capabilities = caps;
    }

    fn register_mock_builtin_runner(&mut self, runner: &str) {
        let runner_name = CapabilityName(runner.into());
        self.builtin_runner_registrations.push(RunnerRegistration {
            source_name: runner_name.clone(),
            target_name: runner_name.clone(),
            source: RegistrationSource::Self_,
        });
    }

    /// Add a custom capability security policy to restrict routing of certain caps.
    fn add_capability_policy(
        &mut self,
        key: CapabilityAllowlistKey,
        allowlist: HashSet<AllowlistEntry>,
    ) {
        self.capability_policy.insert(key, allowlist);
    }

    /// Add a custom debug capability security policy to restrict routing of certain caps.
    fn add_debug_capability_policy(
        &mut self,
        key: DebugCapabilityKey,
        allowlist: HashSet<DebugCapabilityAllowlistEntry>,
    ) {
        self.debug_capability_policy.insert(key, allowlist);
    }

    fn set_component_id_index_path(&mut self, index_path: String) {
        self.component_id_index_path = Some(index_path);
    }

    async fn build(self) -> RoutingTestForAnalyzer {
        let mut config = RuntimeConfig::default();
        config.root_component_url = Some(self.root_url.clone());
        config.namespace_capabilities = self.namespace_capabilities;
        config.builtin_capabilities = self.builtin_capabilities;

        let mut security_policy = SecurityPolicy::default();
        security_policy.capability_policy = self.capability_policy;
        security_policy.debug_capability_policy = self.debug_capability_policy;
        config.security_policy = security_policy;

        config.component_id_index_path = self.component_id_index_path;
        let component_id_index = match config.component_id_index_path {
            Some(ref index_path) => ComponentIdIndex::new(index_path)
                .await
                .expect(&format!("failed to create component ID index with path {}", index_path)),
            None => ComponentIdIndex::default(),
        };
        config.builtin_boot_resolver = self.builtin_boot_resolver;

        let root_url = Url::parse(self.root_url.as_str()).unwrap();
        let build_model_result = ModelBuilderForAnalyzer::new(root_url).build(
            self.decls_by_url,
            Arc::new(config),
            Arc::new(component_id_index),
            RunnerRegistry::from_decl(&self.builtin_runner_registrations),
        );
        let model = build_model_result.model.expect("failed to build ComponentModelForAnalyzer");
        RoutingTestForAnalyzer { model }
    }
}

#[derive(Debug, Error)]
pub enum TestModelError {
    #[error("matching use decl not found")]
    UseDeclNotFound,
    #[error("matching expose decl not found")]
    ExposeDeclNotFound,
}

impl TestModelError {
    pub fn as_zx_status(&self) -> zx_status::Status {
        match self {
            Self::UseDeclNotFound | Self::ExposeDeclNotFound => zx_status::Status::NOT_FOUND,
        }
    }
}

impl RoutingTestForAnalyzer {
    fn assert_event_stream_scope(
        &self,
        use_decl: &UseEventStreamDecl,
        scope: &Vec<ComponentEventRoute>,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) {
        // Perform secondary routing to find scope
        let mut map = vec![];
        ComponentModelForAnalyzer::route_event_stream_sync(use_decl.clone(), &target, &mut map)
            .expect("Expected event_stream routing to succeed.");
        let mut route = use_decl
            .scope
            .as_ref()
            .map(|scope| {
                let route = ComponentEventRoute {
                    component: USE_TARGET_PLACEHOLDER_NAME.to_string(),
                    scope: Some(
                        scope
                            .iter()
                            .map(|s| match s {
                                cm_rust::EventScope::Child(child) => child.name.to_string(),
                                cm_rust::EventScope::Collection(collection) => {
                                    collection.to_string()
                                }
                            })
                            .collect(),
                    ),
                };
                vec![route]
            })
            .unwrap_or_default();
        map.reverse();
        let search_name = use_decl.source_name.clone();

        // Generate a unified route from the component topology
        generate_unified_route(map, search_name, &mut route);
        assert_eq!(scope, &route);
    }

    fn find_matching_use(
        &self,
        check: CheckUse,
        decl: &ComponentDecl,
    ) -> (Result<UseDecl, TestModelError>, ExpectedResult) {
        match check {
            CheckUse::Directory { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Directory(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::Event { request, expected_res, .. } => {
                let find_decl = decl.uses.iter().find_map(|u| match u {
                    UseDecl::Event(d) if (d.target_name == request.event_name) => Some(d.clone()),
                    _ => None,
                });
                let decl_result = match find_decl {
                    Some(d) => Ok(UseDecl::Event(d)),
                    None => Err(TestModelError::UseDeclNotFound),
                };
                (decl_result, expected_res)
            }
            CheckUse::Protocol { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Protocol(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::Service { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Service(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::Storage { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Storage(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::StorageAdmin { expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Protocol(d)
                            if d.source_name.to_string()
                                == fsys::StorageAdminMarker::PROTOCOL_NAME =>
                        {
                            Some(u.clone())
                        }
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::EventStream { path, scope: _, name, expected_res } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::EventStream(d)
                            if d.source_name.to_string() == name.to_string()
                                && path == d.target_path =>
                        {
                            Some(u.clone())
                        }
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
        }
    }

    fn find_matching_expose(
        &self,
        check: CheckUse,
        decl: &ComponentDecl,
    ) -> (Result<ExposeDecl, TestModelError>, ExpectedResult) {
        match check {
            CheckUse::Directory { path, expected_res, .. }
            | CheckUse::Protocol { path, expected_res, .. }
            | CheckUse::EventStream { path, expected_res, .. }
            | CheckUse::Service { path, expected_res, .. } => (
                decl.exposes
                    .iter()
                    .find(|&e| e.target_name().to_string() == path.basename)
                    .cloned()
                    .ok_or(TestModelError::ExposeDeclNotFound),
                expected_res,
            ),
            CheckUse::Event { .. } | CheckUse::Storage { .. } | CheckUse::StorageAdmin { .. } => {
                panic!("attempted to use from expose for unsupported capability type")
            }
        }
    }
}

/// Converts a component framework route to a strongly-typed stringified route
/// which can be compared against a string of paths for testing purposes.
fn generate_unified_route(
    map: Vec<Arc<ComponentInstanceForAnalyzer>>,
    mut search_name: CapabilityName,
    route: &mut Vec<ComponentEventRoute>,
) {
    for component in &map {
        add_component_to_route(component, &mut search_name, route);
    }
}

/// Adds a specified component to the route
fn add_component_to_route(
    component: &Arc<ComponentInstanceForAnalyzer>,
    search_name: &mut CapabilityName,
    route: &mut Vec<ComponentEventRoute>,
) {
    let locked_state = component.lock_resolved_state().now_or_never().unwrap().unwrap();
    let offers = locked_state.offers();
    let exposes = locked_state.exposes();
    let mut component_route = ComponentEventRoute {
        component: if let Some(moniker) = component.child_moniker() {
            moniker.name().to_string()
        } else {
            "/".to_string()
        },
        scope: None,
    };
    scan_event_stream_offers(offers, search_name, &mut component_route);
    scan_event_stream_exposes(exposes, search_name, &mut component_route);
    route.push(component_route);
}

/// Scans exposes for event streams and serializes any scopes that are found to the component_route
fn scan_event_stream_exposes(
    exposes: Vec<ExposeDecl>,
    search_name: &mut CapabilityName,
    component_route: &mut ComponentEventRoute,
) {
    for expose in exposes {
        // Found match, continue up tree.
        if let cm_rust::ExposeDecl::EventStream(stream) = expose {
            if stream.source_name == *search_name {
                if let Some(scopes) = stream.scope {
                    component_route.scope = Some(
                        scopes
                            .iter()
                            .map(|s| match s {
                                cm_rust::EventScope::Child(child) => child.name.to_string(),
                                cm_rust::EventScope::Collection(collection) => {
                                    collection.to_string()
                                }
                            })
                            .collect(),
                    );
                }
                *search_name = stream.target_name;
            }
        }
    }
}

/// Scans offers for event streams and serializes any scopes that are found to the component route
fn scan_event_stream_offers(
    offers: Vec<OfferDecl>,
    search_name: &mut CapabilityName,
    component_route: &mut ComponentEventRoute,
) {
    for offer in offers {
        if let cm_rust::OfferDecl::EventStream(stream) = offer {
            // Found match, continue up tree.
            if stream.source_name == *search_name {
                if let Some(scopes) = stream.scope {
                    component_route.scope = Some(
                        scopes
                            .iter()
                            .map(|s| match s {
                                cm_rust::EventScope::Child(child) => child.name.to_string(),
                                cm_rust::EventScope::Collection(collection) => {
                                    collection.to_string()
                                }
                            })
                            .collect(),
                    );
                }
                *search_name = stream.target_name;
            }
        }
    }
}

#[async_trait]
impl RoutingTestModel for RoutingTestForAnalyzer {
    type C = ComponentInstanceForAnalyzer;

    async fn check_use(&self, moniker: AbsoluteMoniker, check: CheckUse) {
        let target_id = NodePath::new(moniker.path().clone());
        let target = self.model.get_instance(&target_id).expect("target instance not found");
        let scope =
            if let CheckUse::EventStream { path: _, ref scope, name: _, expected_res: _ } = check {
                Some(scope.clone())
            } else {
                None
            };
        let (find_decl, expected) = self.find_matching_use(check, target.decl_for_testing());

        // If `find_decl` is not OK, check that `expected` has a matching error.
        // Otherwise, route the capability and compare the result to `expected`.
        match &find_decl {
            Err(err) => {
                match expected {
                    ExpectedResult::Ok => panic!("expected UseDecl was not found: {}", err),
                    ExpectedResult::Err(status) => {
                        assert_eq!(err.as_zx_status(), status);
                    }
                    ExpectedResult::ErrWithNoEpitaph => {}
                };
                return;
            }
            Ok(use_decl) => {
                for result in self.model.check_use_capability(use_decl, &target).iter() {
                    match result.result {
                        Err(ref err) => match expected {
                            ExpectedResult::Ok => {
                                panic!("routing failed, expected success: {:?}", err)
                            }
                            ExpectedResult::Err(status) => {
                                assert_eq!(err.as_zx_status(), status);
                            }
                            ExpectedResult::ErrWithNoEpitaph => {}
                        },
                        Ok(_) => match expected {
                            ExpectedResult::Ok => {
                                if let UseDecl::EventStream(use_decl) = use_decl {
                                    self.assert_event_stream_scope(
                                        use_decl,
                                        scope
                                            .as_ref()
                                            .expect("scope should be non-null for event streams"),
                                        &target,
                                    );
                                }
                            }
                            _ => panic!("capability use succeeded, expected failure"),
                        },
                    }
                }
            }
        }
    }

    async fn check_use_exposed_dir(&self, moniker: AbsoluteMoniker, check: CheckUse) {
        let target =
            self.model.get_instance(&NodePath::from(moniker)).expect("target instance not found");

        let (find_decl, expected) = self.find_matching_expose(check, target.decl_for_testing());

        // If `find_decl` is not OK, check that `expected` has a matching error.
        // Otherwise, route the capability and compare the result to `expected`.
        match &find_decl {
            Err(err) => {
                match expected {
                    ExpectedResult::Ok => panic!("expected ExposeDecl was not found: {}", err),
                    ExpectedResult::Err(status) => {
                        assert_eq!(err.as_zx_status(), status);
                    }
                    _ => unimplemented![],
                };
                return;
            }
            Ok(expose_decl) => {
                match self
                    .model
                    .check_use_exposed_capability(expose_decl, &target)
                    .expect("expected result for exposed directory")
                    .result
                {
                    Err(CapabilityRouteError::AnalyzerModelError(err)) => match expected {
                        ExpectedResult::Ok => panic!("routing failed, expected success"),
                        ExpectedResult::Err(status) => {
                            assert_eq!(err.as_zx_status(), status);
                        }
                        _ => unimplemented![],
                    },
                    Err(_) => panic!("expected CapabilityRouteError::AnalyzerModelError"),
                    Ok(_) => match expected {
                        ExpectedResult::Ok => {}
                        _ => panic!("capability use succeeded, expected failure"),
                    },
                }
            }
        }
    }

    async fn look_up_instance(
        &self,
        moniker: &AbsoluteMoniker,
    ) -> Result<Arc<ComponentInstanceForAnalyzer>, anyhow::Error> {
        self.model.get_instance(&NodePath::from(moniker.clone())).map_err(|err| anyhow!(err))
    }

    // File and directory operations
    //
    // All file and directory operations are no-ops for the static model.
    #[allow(unused_variables)]
    async fn check_open_file(&self, moniker: AbsoluteMoniker, path: CapabilityPath) {}

    #[allow(unused_variables)]
    async fn create_static_file(&self, path: &Path, contents: &str) -> Result<(), anyhow::Error> {
        Ok(())
    }

    #[allow(unused_variables)]
    fn install_namespace_directory(&self, path: &str) {}

    #[allow(unused_variables)]
    fn add_subdir_to_data_directory(&self, subdir: &str) {}

    #[allow(unused_variables)]
    async fn check_test_subdir_contents(&self, path: &str, expected: Vec<String>) {}

    #[allow(unused_variables)]
    async fn check_namespace_subdir_contents(&self, path: &str, expected: Vec<String>) {}

    #[allow(unused_variables)]
    async fn check_test_subdir_contains(&self, path: &str, expected: String) {}

    #[allow(unused_variables)]
    async fn check_test_dir_tree_contains(&self, expected: String) {}
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_rust::{EventScope, EventStreamDecl, OfferEventStreamDecl, UseEventStreamDecl},
        routing_test_helpers::instantiate_common_routing_tests,
        std::str::FromStr,
    };

    instantiate_common_routing_tests! { RoutingTestBuilderForAnalyzer }

    // Routing of service capabilities is tested as part of the common routing tests
    // generated by `instantiate_common_routing_tests`.
    //
    // In order to test additional validation beyond routing, we need much more setup for component
    // manager's routing model than we do for the static analyzer's model. The following tests
    // suffice to test the static analyzer's `check_use_capability()` method for service capabilities.

    ///   a
    ///  /
    /// b
    ///
    /// a: offer to b from self
    /// b: use from parent
    #[fuchsia::test]
    async fn check_use_service_from_parent() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Parent,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
            availability: Availability::Required,
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Service(OfferServiceDecl {
                        source: OfferSource::Self_,
                        source_name: "foo".into(),
                        source_instance_filter: None,
                        renamed_instances: None,
                        target_name: "foo".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        availability: Availability::Required,
                    }))
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone().into()).build()),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await
    }

    ///   a
    ///  /
    /// b
    ///
    /// a: offer to b from self
    /// b: use from parent, but parent component is not executable
    #[fuchsia::test]
    async fn check_service_source_is_executable() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Parent,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
            availability: Availability::Required,
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new_empty_component()
                    .offer(OfferDecl::Service(OfferServiceDecl {
                        source: OfferSource::Self_,
                        source_name: "foo".into(),
                        source_instance_filter: None,
                        renamed_instances: None,
                        target_name: "foo".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        availability: Availability::Required,
                    }))
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone().into()).build()),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Err(zx_status::Status::UNAVAILABLE),
                },
            )
            .await
    }

    ///   a
    ///  /
    /// b
    ///
    /// a: use from b
    /// b: expose to parent from self
    #[fuchsia::test]
    async fn check_use_service_from_child() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Child("b".to_string()),
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
            availability: Availability::Required,
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .use_(use_decl.clone().into())
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .expose(ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// a: offer to b from child c
    /// b: use from parent
    /// c: expose from self
    #[fuchsia::test]
    async fn check_use_service_from_sibling() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Parent,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
            availability: Availability::Required,
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Service(OfferServiceDecl {
                        source: OfferSource::static_child("c".into()),
                        source_name: "foo".into(),
                        source_instance_filter: None,
                        renamed_instances: None,
                        target_name: "foo".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone().into()).build()),
            (
                "c",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .build(),
            ),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: declares runner "elf" as service "/svc/runner" from self.
    /// a: registers runner "elf" from realm in environment as "hobbit".
    /// b: creates environment extending from realm.
    /// c: uses runner "hobbit" in its ProgramDecl.
    #[fuchsia::test]
    async fn check_program_runner_from_inherited_environment() {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_runner(RunnerRegistration {
                                source_name: "elf".into(),
                                source: RegistrationSource::Self_,
                                target_name: "hobbit".into(),
                            })
                            .build(),
                    )
                    .runner(RunnerDecl {
                        name: "elf".into(),
                        source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
                    })
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .build(),
                    )
                    .build(),
            ),
            ("c", ComponentDeclBuilder::new_empty_component().add_program("hobbit").build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let c_component = test.look_up_instance(&vec!["b", "c"].into()).await.expect("c instance");

        assert!(test
            .model
            .check_program_runner(
                c_component.decl_for_testing().program.as_ref().expect("missing program decl"),
                &c_component
            )
            .expect("expected results of program runner check")
            .result
            .is_ok());
    }

    /*
    TODO(https://fxbug.dev/107902): Allow exposing from parent.
    /// Tests exposing an event_stream from a child through its parent down to another
    /// unrelated child.
    ///        a
    ///         \
    ///          b
    ///          /\
    ///          c f
    ///          /\
    ///          d e
    /// c exposes started with a scope of e (but not d)
    /// to b, which then offers that to f.
    #[fuchsia::test]
    pub async fn test_expose_event_stream_with_scope_2() {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::EventStream(OfferEventStreamDecl {
                        source: OfferSource::Child(ChildRef {
                            name: "c".to_string(),
                            collection: None,
                        }),
                        source_name: "started".into(),
                        scope: None,
                        filter: None,
                        target: OfferTarget::Child(ChildRef {
                            name: "f".to_string(),
                            collection: None,
                        }),
                        target_name: CapabilityName::from("started"),
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("c")
                    .add_lazy_child("f")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::EventStream(ExposeEventStreamDecl {
                        source: ExposeSource::Framework,
                        source_name: "started".into(),
                        scope: Some(vec![EventScope::Child(ChildRef {
                            name: "e".to_string(),
                            collection: None,
                        })]),
                        target: ExposeTarget::Parent,
                        target_name: CapabilityName::from("started"),
                    }))
                    .add_lazy_child("d")
                    .add_lazy_child("e")
                    .build(),
            ),
            ("d", ComponentDeclBuilder::new().build()),
            ("e", ComponentDeclBuilder::new().build()),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        source: UseSource::Parent,
                        filter: None,
                        source_name: "started".into(),
                        target_path: CapabilityPath::from_str("/event/stream").unwrap(),
                        scope: None,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_builtin_capabilities(vec![CapabilityDecl::EventStream(EventStreamDecl {
            name: "started".into(),
        })]);

        let model = builder.build().await;
        model
            .check_use(
                vec!["b", "f"].into(),
                CheckUse::EventStream {
                    expected_res: ExpectedResult::Ok,
                    path: CapabilityPath::from_str("/event/stream").unwrap(),
                    scope: vec![
                        ComponentEventRoute {
                            component: "c".to_string(),
                            scope: Some(vec!["e".to_string()]),
                        },
                        ComponentEventRoute { component: "b".to_string(), scope: None },
                    ],
                    name: "started".into(),
                },
            )
            .await;
    }
    */

    ///   a
    ///    \
    ///     b
    ///
    /// b: uses framework events "started", and "capability_requested"
    #[fuchsia::test]
    pub async fn test_use_event_stream_from_above_root_2() {
        let components = vec![(
            "a",
            ComponentDeclBuilder::new()
                .use_(UseDecl::EventStream(UseEventStreamDecl {
                    source: UseSource::Parent,
                    filter: None,
                    source_name: "started".into(),
                    target_path: CapabilityPath::from_str("/event/stream").unwrap(),
                    scope: None,
                    availability: Availability::Required,
                }))
                .build(),
        )];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_builtin_capabilities(vec![CapabilityDecl::EventStream(EventStreamDecl {
            name: "started".into(),
        })]);

        let model = builder.build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::EventStream {
                    expected_res: ExpectedResult::Ok,
                    path: CapabilityPath::from_str("/event/stream").unwrap(),
                    scope: vec![],
                    name: "started".into(),
                },
            )
            .await;
    }

    ///   a
    ///   /\
    ///  b  c
    ///    / \
    ///   d   e
    /// c: uses framework events "started", and "capability_requested",
    /// scoped to b and c.
    /// d receives started which is scoped to b, c, and e.
    #[fuchsia::test]
    pub async fn test_use_event_stream_from_above_root_and_downscoped_2() {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::EventStream(OfferEventStreamDecl {
                        source: OfferSource::Parent,
                        source_name: "started".into(),
                        scope: Some(vec![
                            EventScope::Child(ChildRef { name: "b".to_string(), collection: None }),
                            EventScope::Child(ChildRef { name: "c".to_string(), collection: None }),
                        ]),
                        filter: None,
                        target: OfferTarget::Child(ChildRef {
                            name: "b".to_string(),
                            collection: None,
                        }),
                        target_name: CapabilityName::from("started"),
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::EventStream(OfferEventStreamDecl {
                        source: OfferSource::Parent,
                        source_name: "started".into(),
                        scope: Some(vec![
                            EventScope::Child(ChildRef { name: "b".to_string(), collection: None }),
                            EventScope::Child(ChildRef { name: "c".to_string(), collection: None }),
                        ]),
                        filter: None,
                        target: OfferTarget::Child(ChildRef {
                            name: "c".to_string(),
                            collection: None,
                        }),
                        target_name: CapabilityName::from("started"),
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        source: UseSource::Parent,
                        filter: None,
                        source_name: "started".into(),
                        target_path: CapabilityPath::from_str("/event/stream").unwrap(),
                        scope: None,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        source: UseSource::Parent,
                        filter: None,
                        source_name: "started".into(),
                        target_path: CapabilityPath::from_str("/event/stream").unwrap(),
                        scope: None,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::EventStream(OfferEventStreamDecl {
                        source: OfferSource::Parent,
                        source_name: "started".into(),
                        scope: Some(vec![EventScope::Child(ChildRef {
                            name: "e".to_string(),
                            collection: None,
                        })]),
                        filter: None,
                        target: OfferTarget::Child(ChildRef {
                            name: "d".to_string(),
                            collection: None,
                        }),
                        target_name: CapabilityName::from("started"),
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("d")
                    .add_lazy_child("e")
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        source: UseSource::Parent,
                        source_name: "started".into(),
                        target_path: CapabilityPath::from_str("/event/stream").unwrap(),
                        scope: None,
                        filter: None,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            ("e", ComponentDeclBuilder::new().build()),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_builtin_capabilities(vec![CapabilityDecl::EventStream(EventStreamDecl {
            name: "started".into(),
        })]);

        let model = builder.build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::EventStream {
                    expected_res: ExpectedResult::Ok,
                    path: CapabilityPath::from_str("/event/stream").unwrap(),
                    scope: vec![ComponentEventRoute {
                        component: "/".to_string(),
                        scope: Some(vec!["b".to_string(), "c".to_string()]),
                    }],
                    name: "started".into(),
                },
            )
            .await;
        model
            .check_use(
                vec!["c"].into(),
                CheckUse::EventStream {
                    expected_res: ExpectedResult::Ok,
                    path: CapabilityPath::from_str("/event/stream").unwrap(),
                    scope: vec![ComponentEventRoute {
                        component: "/".to_string(),
                        scope: Some(vec!["b".to_string(), "c".to_string()]),
                    }],
                    name: "started".into(),
                },
            )
            .await;
        model
            .check_use(
                vec!["c", "d"].into(), // Should get e's event from parent
                CheckUse::EventStream {
                    expected_res: ExpectedResult::Ok,
                    path: CapabilityPath::from_str("/event/stream").unwrap(),
                    scope: vec![
                        ComponentEventRoute {
                            component: "/".to_string(),
                            scope: Some(vec!["b".to_string(), "c".to_string()]),
                        },
                        ComponentEventRoute {
                            component: "c".to_string(),
                            scope: Some(vec!["e".to_string()]),
                        },
                    ],
                    name: "started".into(),
                },
            )
            .await;
    }

    ///  a
    ///   \
    ///    b
    ///
    /// a: declares runner "elf" with service "/svc/runner" from "self".
    /// a: registers runner "elf" from self in environment as "hobbit".
    /// b: uses runner "hobbit" in its ProgramDecl. Fails because "hobbit" was not in environment.
    #[fuchsia::test]
    async fn check_program_runner_from_environment_not_found() {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_runner(RunnerRegistration {
                                source_name: "elf".into(),
                                source: RegistrationSource::Self_,
                                target_name: "dwarf".into(),
                            })
                            .build(),
                    )
                    .runner(RunnerDecl {
                        name: "elf".into(),
                        source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
                    })
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new_empty_component().add_program("hobbit").build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let check_result = test
            .model
            .check_program_runner(
                &b_component.decl_for_testing().program.as_ref().expect("missing program decl"),
                &b_component,
            )
            .expect("expected result of program runner check");

        assert_matches!(
            check_result.result,
            Err(CapabilityRouteError::AnalyzerModelError(
                AnalyzerModelError::RoutingError(
                    RoutingError::UseFromEnvironmentNotFound {
                    moniker,
                    capability_type,
                    capability_name,
            })))
                if moniker == *b_component.abs_moniker() &&
                capability_type == "runner" &&
                capability_name == CapabilityName("hobbit".to_string())
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: creates environment "env" and registers resolver "base" in "env" from self.
    /// b: has environment "env" and is resolved by the "base" resolver.
    #[fuchsia::test]
    async fn check_resolver_from_extended_environment() {
        let a_url = make_test_url("a");
        let b_url = "base://b/".to_string();

        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new().name("b").url(&b_url).environment("env"))
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_resolver(ResolverRegistration {
                                resolver: "base".into(),
                                source: RegistrationSource::Self_,
                                scheme: "base".into(),
                            }),
                    )
                    .resolver(ResolverDecl {
                        name: "base".into(),
                        source_path: Some(
                            "/svc/fuchsia.component.resolution.Resolver".parse().unwrap(),
                        ),
                    })
                    .build(),
            ),
            (b_url, ComponentDeclBuilder::new_empty_component().build()),
        ];

        let test =
            RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");

        let result = test.model.check_resolver(&b_component);
        assert!(result.result.is_ok());
        assert_eq!(result.using_node, NodePath::absolute_from_vec(vec!["b"]));
        assert_eq!(result.capability, "base");
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: creates environment "b_env" and registers resolver "base" in "b_env" from self.
    /// b: inherits environment "b_env" but creates a new empty environment "c_env" for c.
    /// c: doesn't inherit the "base" resolver.
    #[fuchsia::test]
    async fn check_resolver_from_grandparent_environment_not_found() {
        let a_url = make_test_url("a");
        let b_url = make_test_url("b");
        let c_url = "base://c/".to_string();

        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("b_env"))
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("b_env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_resolver(ResolverRegistration {
                                resolver: "base".into(),
                                source: RegistrationSource::Self_,
                                scheme: "base".into(),
                            }),
                    )
                    .resolver(ResolverDecl {
                        name: "base".into(),
                        source_path: Some(
                            "/svc/fuchsia.component.resolution.Resolver".parse().unwrap(),
                        ),
                    })
                    .build(),
            ),
            (
                b_url,
                ComponentDeclBuilder::new_empty_component()
                    .add_child(ChildDeclBuilder::new().name("c").url(&c_url).environment("c_env"))
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("c_env")
                            .extends(fdecl::EnvironmentExtends::None),
                    )
                    .build(),
            ),
            (c_url, ComponentDeclBuilder::new_empty_component().build()),
        ];

        let test =
            RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components).build().await;
        let c_component = test.look_up_instance(&vec!["b", "c"].into()).await.expect("c instance");

        let result = test.model.check_resolver(&c_component);

        assert_matches!(
            &result.result,
            Err(CapabilityRouteError::AnalyzerModelError(
                AnalyzerModelError::MissingResolverForScheme(
                    resolver
                )))
                if resolver == "base"
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: has the standard boot resolver registered in its environment, but
    ///    the resolver is not provided as a built-in capability.
    /// b: is resolved by the standard boot resolver.
    #[fuchsia::test]
    async fn check_resolver_from_builtin_environment_not_found() {
        let a_url = make_test_url("a");
        let b_url = format!("{}://b/", BOOT_SCHEME);

        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(
                        ChildDeclBuilder::new().name("b").url(&format!("{}://b", BOOT_SCHEME)),
                    )
                    .build(),
            ),
            (b_url, ComponentDeclBuilder::new().build()),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components);
        builder.set_builtin_boot_resolver(component_internal::BuiltinBootResolver::Boot);
        let test = builder.build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");

        let result = test.model.check_resolver(&b_component);

        assert_matches!(
        &result.result,
            Err(CapabilityRouteError::AnalyzerModelError(
                AnalyzerModelError::RoutingError(
                    RoutingError::UseFromComponentManagerNotFound{
                        capability_id: resolver
            })))
                if resolver == BOOT_RESOLVER_NAME
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: offers protocol /svc/foo from self as /svc/bar
    /// b: uses protocol /svc/bar as /svc/hippo
    #[fuchsia::test]
    async fn map_route_use_from_parent() {
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "bar_svc".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let offer_decl = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Self_,
            source_name: "foo_svc".into(),
            target_name: "bar_svc".into(),
            target: OfferTarget::Child(ChildRef { name: "b".to_string(), collection: None }),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let protocol_decl = ProtocolDeclBuilder::new("foo_svc").build();
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .protocol(protocol_decl.clone())
                    .offer(offer_decl.clone())
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone()).build()),
        ];
        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_result = test.model.check_use_capability(&use_decl, &b_component);
        assert_eq!(route_result.len(), 1);
        let route_map = route_result[0].result.clone().expect("expected OK route");

        assert_eq!(
            route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Protocol(protocol_decl)
                }
            ]
        )
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: uses protocol /svc/bar from b as /svc/hippo
    /// b: exposes protocol /svc/foo from self as /svc/bar
    #[fuchsia::test]
    async fn map_route_use_from_child() {
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Child("b".to_string()),
            source_name: "bar_svc".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let expose_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "foo_svc".into(),
            target_name: "bar_svc".into(),
            target: ExposeTarget::Parent,
        });
        let protocol_decl = ProtocolDeclBuilder::new("foo_svc").build();
        let components = vec![
            ("a", ComponentDeclBuilder::new().use_(use_decl.clone()).add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .protocol(protocol_decl.clone())
                    .expose(expose_decl.clone())
                    .build(),
            ),
        ];
        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let a_component = test.look_up_instance(&vec![].into()).await.expect("a instance");
        let route_results = test.model.check_use_capability(&use_decl, &a_component);
        assert_eq!(route_results.len(), 1);
        let route_map = route_results[0].result.clone().expect("expected OK route");

        assert_eq!(
            route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: use_decl
                },
                RouteSegment::ExposeBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: expose_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: CapabilityDecl::Protocol(protocol_decl)
                }
            ]
        )
    }

    /// a: uses protocol /svc/hippo from self
    #[fuchsia::test]
    async fn map_route_use_from_self() {
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Self_,
            source_name: "hippo".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let protocol_decl = ProtocolDeclBuilder::new("hippo").build();
        let components = vec![(
            "a",
            ComponentDeclBuilder::new()
                .protocol(protocol_decl.clone())
                .use_(use_decl.clone())
                .build(),
        )];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let a_component = test.look_up_instance(&vec![].into()).await.expect("a instance");
        let route_results = test.model.check_use_capability(&use_decl, &a_component);
        assert_eq!(route_results.len(), 1);
        let route_map = route_results[0].result.clone().expect("expected OK route");

        assert_eq!(
            route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: use_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Protocol(protocol_decl)
                }
            ]
        )
    }

    ///     a
    ///    / \
    ///   b   c
    ///  /
    /// d
    ///
    /// d: exposes directory /data/foo from self as /data/bar
    /// b: exposes directory /data/bar from d as /data/baz
    /// a: offers directory /data/baz from b as /data/foobar to c
    /// c: uses /data/foobar as /data/hippo
    #[fuchsia::test]
    async fn map_route_use_from_niece() {
        let use_decl = UseDecl::Directory(UseDirectoryDecl {
            source: UseSource::Parent,
            source_name: "foobar_data".into(),
            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
            rights: *READ_RIGHTS,
            subdir: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let a_offer_decl = OfferDecl::Directory(OfferDirectoryDecl {
            source: OfferSource::static_child("b".to_string()),
            source_name: "baz_data".into(),
            target_name: "foobar_data".into(),
            target: OfferTarget::static_child("c".to_string()),
            rights: Some(*READ_RIGHTS),
            subdir: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let b_expose_decl = ExposeDecl::Directory(ExposeDirectoryDecl {
            source: ExposeSource::Child("d".to_string()),
            source_name: "bar_data".into(),
            target_name: "baz_data".into(),
            target: ExposeTarget::Parent,
            rights: Some(*READ_RIGHTS),
            subdir: None,
        });
        let d_expose_decl = ExposeDecl::Directory(ExposeDirectoryDecl {
            source: ExposeSource::Self_,
            source_name: "foo_data".into(),
            target_name: "bar_data".into(),
            target: ExposeTarget::Parent,
            rights: Some(*READ_RIGHTS),
            subdir: None,
        });
        let directory_decl = DirectoryDeclBuilder::new("foo_data").build();

        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(a_offer_decl.clone())
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .expose(b_expose_decl.clone())
                    .add_lazy_child("d")
                    .build(),
            ),
            ("c", ComponentDeclBuilder::new().use_(use_decl.clone()).build()),
            (
                "d",
                ComponentDeclBuilder::new()
                    .directory(directory_decl.clone())
                    .expose(d_expose_decl.clone())
                    .build(),
            ),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let c_component = test.look_up_instance(&vec!["c"].into()).await.expect("c instance");
        let route_results = test.model.check_use_capability(&use_decl, &c_component);
        assert_eq!(route_results.len(), 1);
        let route_map = route_results[0].result.clone().expect("expected OK route");

        assert_eq!(
            route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["c"]),
                    capability: use_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: a_offer_decl
                },
                RouteSegment::ExposeBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: b_expose_decl
                },
                RouteSegment::ExposeBy {
                    node_path: NodePath::absolute_from_vec(vec!["b", "d"]),
                    capability: d_expose_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec!["b", "d"]),
                    capability: CapabilityDecl::Directory(directory_decl),
                }
            ]
        )
    }

    ///  a
    ///   \
    ///    b
    ///
    /// a: declares runner "elf" with service "/svc/runner" from "self".
    /// a: registers runner "elf" from self in environment as "hobbit".
    /// b: refers to runner "hobbit" in its `ProgramDecl`.
    #[fuchsia::test]
    async fn map_route_for_program_runner() {
        let runner_reg = RunnerRegistration {
            source_name: "elf".into(),
            source: RegistrationSource::Self_,
            target_name: "hobbit".into(),
        };
        let runner_decl = RunnerDecl {
            name: "elf".into(),
            source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_runner(runner_reg.clone())
                            .build(),
                    )
                    .runner(runner_decl.clone())
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new_empty_component().add_program("hobbit").build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_map = test
            .model
            .check_program_runner(
                &b_component
                    .decl_for_testing()
                    .program
                    .as_ref()
                    .expect("expected ProgramDecl for b"),
                &b_component,
            )
            .expect("expected result of program runner route")
            .result
            .expect("expected OK program runner route");

        assert_eq!(
            route_map,
            vec![
                RouteSegment::RequireRunner {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    runner: "hobbit".into(),
                },
                RouteSegment::RegisterBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: RegistrationDecl::Runner(runner_reg)
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Runner(runner_decl)
                },
            ]
        )
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: has storage decl with name "cache" with a source of self at path /data
    /// a: offers cache storage to b from "mystorage"
    /// b: uses cache storage as /storage
    ///
    /// We expect 2 route maps: one for the storage capability and one for the backing
    /// directory.
    #[fuchsia::test]
    async fn map_route_storage_and_dir_from_parent() {
        let directory_decl = DirectoryDeclBuilder::new("data")
            .path("/data")
            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
            .build();
        let storage_decl = StorageDecl {
            name: "cache".into(),
            backing_dir: "data".try_into().unwrap(),
            source: StorageDirectorySource::Self_,
            subdir: None,
            storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
        };
        let offer_storage_decl = OfferDecl::Storage(OfferStorageDecl {
            source: OfferSource::Self_,
            target: OfferTarget::static_child("b".to_string()),
            source_name: "cache".into(),
            target_name: "cache".into(),
            availability: Availability::Required,
        });
        let use_storage_decl = UseDecl::Storage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/storage".try_into().unwrap(),
            availability: Availability::Required,
        });
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .storage(storage_decl.clone())
                    .directory(directory_decl.clone())
                    .offer(offer_storage_decl.clone())
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_storage_decl.clone()).build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_results = test.model.check_use_capability(&use_storage_decl, &b_component);
        assert_eq!(route_results.len(), 2);
        let storage_route_map =
            route_results[0].result.clone().expect("expected OK route for storage capability");
        let backing_directory_route_map = route_results[1]
            .result
            .clone()
            .expect("expected OK route for storage-backing directory");

        assert_eq!(
            storage_route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_storage_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_storage_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Storage(storage_decl.clone())
                }
            ]
        );
        assert_eq!(
            backing_directory_route_map,
            vec![
                RouteSegment::RegisterBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: RegistrationDecl::Storage(storage_decl.into())
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Directory(directory_decl)
                }
            ]
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: offers framework event "started" to b as "started_on_a"
    /// a: offers built-in protocol "fuchsia.sys2.EventSource" to b
    /// b: uses "started_on_a" as "started"
    /// b: uses protocol "fuchsia.sys2.EventSource"
    #[fuchsia::test]
    async fn route_map_use_from_framework_and_builtin() {
        let offer_event_decl = OfferDecl::Event(OfferEventDecl {
            source: OfferSource::Framework,
            source_name: "started".into(),
            target_name: "started_on_a".into(),
            target: OfferTarget::static_child("b".to_string()),
            filter: None,
            availability: Availability::Required,
        });
        let use_event_decl = UseDecl::Event(UseEventDecl {
            source: UseSource::Parent,
            source_name: "started_on_a".into(),
            target_name: "started".into(),
            filter: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let offer_event_source_decl = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
            target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
            target: OfferTarget::static_child("b".to_string()),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let use_event_source_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
            target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let event_source_decl = CapabilityDecl::Protocol(ProtocolDecl {
            name: "fuchsia.sys2.EventSource".into(),
            source_path: None,
        });

        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(offer_event_decl.clone())
                    .offer(offer_event_source_decl.clone())
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(use_event_source_decl.clone())
                    .use_(use_event_decl.clone())
                    .build(),
            ),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_builtin_capabilities(vec![event_source_decl.clone()]);
        let test = builder.build().await;

        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let event_route_results = test.model.check_use_capability(&use_event_decl, &b_component);
        assert_eq!(event_route_results.len(), 1);
        let event_route_map = event_route_results[0].result.clone().expect("expected OK route");

        assert_eq!(
            event_route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_event_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_event_decl
                },
                RouteSegment::ProvideFromFramework { capability: "started".into() }
            ]
        );

        let event_source_route_results =
            test.model.check_use_capability(&use_event_source_decl, &b_component);
        assert_eq!(event_source_route_results.len(), 1);
        let event_source_route_map =
            event_source_route_results[0].result.clone().expect("expected OK route");

        assert_eq!(
            event_source_route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_event_source_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_event_source_decl
                },
                RouteSegment::ProvideAsBuiltin { capability: event_source_decl }
            ]
        )
    }

    ///  component manager's namespace
    ///   |
    ///   a
    ///    \
    ///     b
    ///
    /// a: offers protocol /offer_from_cm_namespace/svc/foo from component manager's
    ///    namespace as bar_svc
    /// b: uses protocol bar_svc as /svc/hippo
    #[fuchsia::test]
    async fn route_map_offer_from_component_manager_namespace() {
        let offer_decl = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: "foo_svc".into(),
            target_name: "bar_svc".into(),
            target: OfferTarget::static_child("b".to_string()),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "bar_svc".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let capability_decl = CapabilityDecl::Protocol(
            ProtocolDeclBuilder::new("foo_svc").path("/offer_from_cm_namespace/svc/foo").build(),
        );
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new().offer(offer_decl.clone()).add_lazy_child("b").build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone()).build()),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_namespace_capabilities(vec![capability_decl.clone()]);
        let test = builder.build().await;
        test.install_namespace_directory("/offer_from_cm_namespace");

        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_results = test.model.check_use_capability(&use_decl, &b_component);
        assert_eq!(route_results.len(), 1);
        let route_map = route_results[0].result.clone().expect("expected OK route");

        assert_eq!(
            route_map,
            vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_decl
                },
                RouteSegment::ProvideFromNamespace { capability: capability_decl }
            ]
        );
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// a: creates environment "env" and registers resolver "base" from c.
    /// b: resolved by resolver "base" through "env".
    /// c: exposes resolver "base" from self.
    #[fuchsia::test]
    async fn route_map_resolver_from_parent_environment() {
        let a_url = make_test_url("a");
        let b_url = "base://b/".to_string();
        let c_url = make_test_url("c");

        let registration_decl = ResolverRegistration {
            resolver: "base".into(),
            source: RegistrationSource::Child("c".to_string()),
            scheme: "base".into(),
        };
        let expose_decl = ExposeDecl::Resolver(ExposeResolverDecl {
            source: ExposeSource::Self_,
            source_name: "base".into(),
            target: ExposeTarget::Parent,
            target_name: "base".into(),
        });
        let resolver_decl = ResolverDecl {
            name: "base".into(),
            source_path: Some("/svc/fuchsia.component.resolution.Resolver".parse().unwrap()),
        };

        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new_empty_component()
                    .add_child(ChildDeclBuilder::new().name("b").url(&b_url).environment("env"))
                    .add_child(ChildDeclBuilder::new_lazy_child("c"))
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_resolver(registration_decl.clone()),
                    )
                    .build(),
            ),
            (b_url, ComponentDeclBuilder::new().build()),
            (
                c_url,
                ComponentDeclBuilder::new()
                    .expose(expose_decl.clone())
                    .resolver(resolver_decl.clone())
                    .build(),
            ),
        ];

        let test =
            RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");

        let route_map = test.model.check_resolver(&b_component);

        assert_eq!(route_map.using_node, NodePath::absolute_from_vec(vec!["b"]));
        assert_eq!(route_map.capability, "base");
        assert_eq!(
            route_map.result.clone().expect("expected OK route"),
            vec![
                RouteSegment::RequireResolver {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    scheme: "base".to_string(),
                },
                RouteSegment::RegisterBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: RegistrationDecl::Resolver(registration_decl)
                },
                RouteSegment::ExposeBy {
                    node_path: NodePath::absolute_from_vec(vec!["c"]),
                    capability: expose_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec!["c"]),
                    capability: CapabilityDecl::Resolver(resolver_decl)
                }
            ]
        );
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: creates environment "env" and registers resolver "base" from self.
    /// b: has environment "env" extending the realm's environment.
    /// c: inherits "env" and is resolved by "base" from grandparent.
    #[fuchsia::test]
    async fn route_map_resolver_from_grandparent_environment() {
        let a_url = make_test_url("a");
        let b_url = make_test_url("b");
        let c_url = "base://c/".to_string();

        let registration_decl = ResolverRegistration {
            resolver: "base".into(),
            source: RegistrationSource::Self_,
            scheme: "base".into(),
        };
        let resolver_decl = ResolverDecl {
            name: "base".into(),
            source_path: Some("/svc/fuchsia.component.resolution.Resolver".parse().unwrap()),
        };
        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env"))
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_resolver(registration_decl.clone()),
                    )
                    .resolver(resolver_decl.clone())
                    .build(),
            ),
            (
                b_url,
                ComponentDeclBuilder::new_empty_component()
                    .add_child(ChildDeclBuilder::new().name("c").url(&c_url))
                    .build(),
            ),
            (c_url, ComponentDeclBuilder::new_empty_component().build()),
        ];

        let test =
            RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components).build().await;
        let c_component = test.look_up_instance(&vec!["b", "c"].into()).await.expect("c instance");

        let route_map = test.model.check_resolver(&c_component);

        assert_eq!(route_map.using_node, NodePath::absolute_from_vec(vec!["b", "c"]));
        assert_eq!(route_map.capability, "base");
        assert_eq!(
            route_map.result.clone().expect("expected OK route"),
            vec![
                RouteSegment::RequireResolver {
                    node_path: NodePath::absolute_from_vec(vec!["b", "c"]),
                    scheme: "base".to_string(),
                },
                RouteSegment::RegisterBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: RegistrationDecl::Resolver(registration_decl)
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Resolver(resolver_decl)
                }
            ]
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: is provided with the standard built-in boot resolver.
    /// b: is resolved by the standard boot resolver.
    #[fuchsia::test]
    async fn route_map_resolver_from_builtin_environment() {
        let a_url = make_test_url("a");
        let b_url = format!("{}://b/", BOOT_SCHEME);

        let boot_resolver_decl = CapabilityDecl::Resolver(ResolverDecl {
            name: BOOT_RESOLVER_NAME.into(),
            source_path: Some("/builtin/source/path".parse().unwrap()),
        });

        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new().name("b").url(&b_url))
                    .build(),
            ),
            (b_url, ComponentDeclBuilder::new().build()),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components);
        builder.set_builtin_boot_resolver(component_internal::BuiltinBootResolver::Boot);
        builder.set_builtin_capabilities(vec![boot_resolver_decl.clone()]);
        let test = builder.build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");

        let route_map = test.model.check_resolver(&b_component);

        assert_eq!(route_map.using_node, NodePath::absolute_from_vec(vec!["b"]));
        assert_eq!(route_map.capability, BOOT_RESOLVER_NAME);
        assert_eq!(
            route_map.result.clone().expect("expected OK route"),
            vec![
                RouteSegment::RequireResolver {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    scheme: BOOT_SCHEME.to_string(),
                },
                RouteSegment::ProvideAsBuiltin { capability: boot_resolver_decl }
            ]
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: creates environment "env" and registers resolver "test" from self.
    /// b: has environment "env" and a relative url that is resolved by resolver "test" from parent.
    #[fuchsia::test]
    async fn route_map_resolver_relative_child_url() {
        let a_url = make_test_url("a");
        let b_relative = "#b";
        let b_url = format!("{}{}", a_url, b_relative);

        let resolver_registration = ResolverRegistration {
            resolver: "test".into(),
            source: RegistrationSource::Self_,
            scheme: "test".into(),
        };
        let resolver_decl = ResolverDecl {
            name: "test".into(),
            source_path: Some("/svc/fuchsia.component.resolution.Resolver".parse().unwrap()),
        };
        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new().name("b").url(b_relative).environment("env"))
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_resolver(resolver_registration.clone()),
                    )
                    .resolver(resolver_decl.clone())
                    .build(),
            ),
            (b_url, ComponentDeclBuilder::new().build()),
        ];

        let test =
            RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");

        let route_map = test.model.check_resolver(&b_component);

        assert_eq!(route_map.using_node, NodePath::absolute_from_vec(vec!["b"]));
        assert_eq!(route_map.capability, "test");
        assert_eq!(
            route_map.result.clone().expect("expected OK route"),
            vec![
                RouteSegment::RequireResolver {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    scheme: "test".to_string(),
                },
                RouteSegment::RegisterBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: RegistrationDecl::Resolver(resolver_registration)
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Resolver(resolver_decl)
                }
            ]
        );
    }

    /// a: is provided with the built-in ELF runner, and requires that runner
    ///    in its `ProgramDecl`.
    #[fuchsia::test]
    async fn route_map_program_runner_from_builtin_environment() {
        let elf_runner_decl = CapabilityDecl::Runner(RunnerDecl {
            name: "elf".into(),
            source_path: Some("/builtin/source/path".parse().unwrap()),
        });
        let component_decl = ComponentDeclBuilder::new_empty_component().add_program("elf").build();

        let components = vec![("a", component_decl.clone())];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_builtin_capabilities(vec![elf_runner_decl.clone()]);
        builder.register_mock_builtin_runner("elf");
        let test = builder.build().await;
        let a_component = test.look_up_instance(&vec![].into()).await.expect("a instance");

        let route_map = test
            .model
            .check_program_runner(
                &component_decl.program.expect("expected ProgramDecl for a"),
                &a_component,
            )
            .expect("expected program runner route")
            .result
            .expect("expected OK route");

        assert_eq!(
            route_map,
            vec![
                RouteSegment::RequireRunner {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    runner: "elf".into(),
                },
                RouteSegment::ProvideAsBuiltin { capability: elf_runner_decl },
            ]
        );
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: Creates environment "env".
    ///    Registers resolver "base" from self.
    ///    Registers runner "hobbit" from as "dwarf" from self.
    ///    Offers directory "foo_data" from self as "bar_data".
    /// b: Has environment "env".
    ///    Requires runner "dwarf", routed successfully from "env".
    ///    Requires resolver "base" to resolve child "c", routed successfully from "env".
    ///    Uses directory "bar_data", routed successfully from parent.
    ///    Exposes "bad_protocol" from child, routing should fail.
    ///    Uses event "started", but routing is not checked because the "event" capability type is not
    ///    selected.
    /// c: is resolved by resolver "base" from grandparent.
    #[fuchsia::test]
    async fn route_maps_all_routes_for_instance() {
        let a_url = make_test_url("a");
        let b_url = "base://b/".to_string();

        let resolver_registration_decl = ResolverRegistration {
            resolver: "base_resolver".into(),
            source: RegistrationSource::Self_,
            scheme: "base".into(),
        };
        let runner_registration_decl = RunnerRegistration {
            source_name: "hobbit".into(),
            source: RegistrationSource::Self_,
            target_name: "dwarf".into(),
        };
        let resolver_decl = ResolverDecl {
            name: "base_resolver".into(),
            source_path: Some("/svc/fuchsia.component.resolution.Resolver".parse().unwrap()),
        };
        let runner_decl = RunnerDecl {
            name: "hobbit".into(),
            source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
        };
        let use_directory_decl = UseDecl::Directory(UseDirectoryDecl {
            source: UseSource::Parent,
            source_name: "bar_data".into(),
            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
            rights: *READ_RIGHTS,
            subdir: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let offer_directory_decl = OfferDecl::Directory(OfferDirectoryDecl {
            source: OfferSource::Self_,
            source_name: "foo_data".into(),
            target_name: "bar_data".into(),
            target: OfferTarget::static_child("b".to_string()),
            rights: Some(*READ_RIGHTS),
            subdir: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let directory_decl = DirectoryDeclBuilder::new("foo_data").build();
        let expose_protocol_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child("c".to_string()),
            source_name: "bad_protocol".into(),
            target_name: "bad_protocol".into(),
            target: ExposeTarget::Parent,
        });
        let use_event_decl = UseDecl::Event(UseEventDecl {
            source: UseSource::Parent,
            source_name: "started_on_a".into(),
            target_name: "started".into(),
            filter: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new().name("b").url(&b_url).environment("env"))
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_resolver(resolver_registration_decl.clone())
                            .add_runner(runner_registration_decl.clone()),
                    )
                    .offer(offer_directory_decl.clone())
                    .directory(directory_decl.clone())
                    .resolver(resolver_decl.clone())
                    .runner(runner_decl.clone())
                    .build(),
            ),
            (
                b_url,
                ComponentDeclBuilder::new_empty_component()
                    .add_program("dwarf")
                    .expose(expose_protocol_decl)
                    .use_(use_directory_decl.clone())
                    .use_(use_event_decl)
                    .build(),
            ),
        ];

        let test =
            RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");

        let route_maps = test.model.check_routes_for_instance(
            &b_component,
            &HashSet::from_iter(
                vec![
                    CapabilityTypeName::Resolver,
                    CapabilityTypeName::Runner,
                    CapabilityTypeName::Directory,
                    CapabilityTypeName::Protocol,
                ]
                .into_iter(),
            ),
        );
        assert_eq!(route_maps.len(), 4);

        let directories =
            route_maps.get(&CapabilityTypeName::Directory).expect("expected directory results");
        assert_eq!(
            directories,
            &vec![VerifyRouteResult {
                using_node: NodePath::absolute_from_vec(vec!["b"]),
                capability: "bar_data".into(),
                result: Ok(vec![
                    RouteSegment::UseBy {
                        node_path: NodePath::absolute_from_vec(vec!["b"]),
                        capability: use_directory_decl,
                    },
                    RouteSegment::OfferBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: offer_directory_decl,
                    },
                    RouteSegment::DeclareBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: CapabilityDecl::Directory(directory_decl),
                    }
                ])
            }]
        );

        let runners = route_maps.get(&CapabilityTypeName::Runner).expect("expected runner results");
        assert_eq!(
            runners,
            &vec![VerifyRouteResult {
                using_node: NodePath::absolute_from_vec(vec!["b"]),
                capability: "dwarf".into(),
                result: Ok(vec![
                    RouteSegment::RequireRunner {
                        node_path: NodePath::absolute_from_vec(vec!["b"]),
                        runner: "dwarf".into(),
                    },
                    RouteSegment::RegisterBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: RegistrationDecl::Runner(runner_registration_decl)
                    },
                    RouteSegment::DeclareBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: CapabilityDecl::Runner(runner_decl)
                    }
                ])
            }]
        );

        let resolvers =
            route_maps.get(&CapabilityTypeName::Resolver).expect("expected resolver results");
        assert_eq!(
            resolvers,
            &vec![VerifyRouteResult {
                using_node: NodePath::absolute_from_vec(vec!["b"]),
                capability: "base_resolver".into(),
                result: Ok(vec![
                    RouteSegment::RequireResolver {
                        node_path: NodePath::absolute_from_vec(vec!["b"]),
                        scheme: "base".to_string(),
                    },
                    RouteSegment::RegisterBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: RegistrationDecl::Resolver(resolver_registration_decl)
                    },
                    RouteSegment::DeclareBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: CapabilityDecl::Resolver(resolver_decl)
                    }
                ])
            }]
        );

        let protocols =
            route_maps.get(&CapabilityTypeName::Protocol).expect("expected protocol results");
        assert_eq!(
            protocols,
            &vec![VerifyRouteResult {
                using_node: NodePath::absolute_from_vec(vec!["b"]),
                capability: "bad_protocol".into(),
                result: Err(CapabilityRouteError::AnalyzerModelError(
                    AnalyzerModelError::RoutingError(
                        RoutingError::ExposeFromChildInstanceNotFound {
                            capability_id: "bad_protocol".to_string(),
                            child_moniker: "c".into(),
                            moniker: b_component.abs_moniker().clone(),
                        },
                    )
                )),
            }],
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: Offers protocol "fuchsia.examples.Echo" from void to b
    /// b: Uses "fuchsia.examples.Echo" optionally
    #[fuchsia::test]
    async fn route_maps_do_not_include_valid_void_routes() {
        let a_url = make_test_url("a");
        let b_url = "base://b/".to_string();

        let use_protocol_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "fuchsia.examples.Echo".into(),
            target_path: CapabilityPath::try_from("/svc/fuchsia.examples.Echo").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Optional,
        });
        let offer_protocol_decl = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Void,
            source_name: "fuchsia.examples.Echo".into(),
            target_name: "fuchsia.examples.Echo".into(),
            target: OfferTarget::static_child("b".to_string()),
            dependency_type: DependencyType::Strong,
            availability: Availability::Optional,
        });

        let components = vec![
            (
                a_url.clone(),
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new().name("b").url(&b_url))
                    .offer(offer_protocol_decl.clone())
                    .build(),
            ),
            (
                b_url,
                ComponentDeclBuilder::new_empty_component()
                    .add_program("dwarf")
                    .use_(use_protocol_decl.clone())
                    .build(),
            ),
        ];

        let test =
            RoutingTestBuilderForAnalyzer::new_with_custom_urls(a_url, components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");

        let route_maps = test.model.check_routes_for_instance(
            &b_component,
            &HashSet::from_iter(vec![CapabilityTypeName::Protocol].into_iter()),
        );
        assert_eq!(route_maps.len(), 1);
        let protocols =
            route_maps.get(&CapabilityTypeName::Protocol).expect("expected protocol results");
        assert_eq!(protocols, &vec![]);
    }
}
