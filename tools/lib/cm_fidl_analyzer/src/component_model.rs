// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_routing::route::RouteSegment,
        component_tree::{
            ComponentNode, ComponentTree, ComponentTreeBuilder, NodeEnvironment, NodePath,
            ResolverRegistry,
        },
    },
    anyhow::anyhow,
    async_trait::async_trait,
    cm_rust::{
        CapabilityDecl, CapabilityName, CapabilityPath, CollectionDecl, ComponentDecl, ExposeDecl,
        OfferDecl, ProgramDecl, RegistrationSource, ResolverRegistration, UseDecl,
    },
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_component_internal as component_internal, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon_status as zx_status,
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase, PartialChildMoniker,
    },
    routing::{
        capability_source::{
            BuiltinCapabilities, CapabilitySourceInterface, ComponentCapability,
            NamespaceCapabilities, StorageCapabilitySource,
        },
        component_id_index::ComponentIdIndex,
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, ResolvedInstanceInterface,
            TopInstanceInterface, WeakExtendedInstanceInterface,
        },
        config::RuntimeConfig,
        environment::{
            component_has_relative_url, find_first_absolute_ancestor_url, DebugRegistry,
            EnvironmentExtends, EnvironmentInterface, RunnerRegistry,
        },
        error::{ComponentInstanceError, RoutingError},
        policy::GlobalPolicyChecker,
        route_capability, route_storage_and_backing_directory, DebugRouteMapper, RegistrationDecl,
        RouteRequest, RouteSource,
    },
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        sync::{Arc, RwLock},
    },
    thiserror::Error,
    url::Url,
};

// Constants used to set up the built-in environment.
pub static BOOT_RESOLVER_NAME: &str = "boot_resolver";
pub static BOOT_SCHEME: &str = "fuchsia-boot";

pub static PKG_RESOLVER_NAME: &str = "package_resolver";
pub static PKG_SCHEME: &str = "fuchsia-pkg";

static REALM_BUILDER_RESOLVER_NAME: &str = "realm_builder_resolver";
static REALM_BUILDER_SCHEME: &str = "realm-builder";

#[derive(Clone, Debug, Error, Deserialize, Serialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum AnalyzerModelError {
    #[error("the source instance `{0}` is not executable")]
    SourceInstanceNotExecutable(String),

    #[error("the capability `{0}` is not a valid source for the capability `{1}`")]
    InvalidSourceCapability(String, String),

    #[error("uses Event capability `{0}` without using the EventSource protocol")]
    MissingEventSourceProtocol(String),

    #[error("no resolver found in environment for scheme `{0}`")]
    MissingResolverForScheme(String),

    #[error(transparent)]
    ComponentInstanceError(#[from] ComponentInstanceError),

    #[error(transparent)]
    RoutingError(#[from] RoutingError),
}

impl AnalyzerModelError {
    pub fn as_zx_status(&self) -> zx_status::Status {
        match self {
            Self::SourceInstanceNotExecutable(_) => zx_status::Status::UNAVAILABLE,
            Self::InvalidSourceCapability(_, _) => zx_status::Status::UNAVAILABLE,
            Self::MissingEventSourceProtocol(_) => zx_status::Status::UNAVAILABLE,
            Self::MissingResolverForScheme(_) => zx_status::Status::NOT_FOUND,
            Self::ComponentInstanceError(err) => err.as_zx_status(),
            Self::RoutingError(err) => err.as_zx_status(),
        }
    }
}

/// Builds a `ComponentModelForAnalyzer` from a `ComponentTree` and a `RuntimeConfig`.
pub struct ModelBuilderForAnalyzer {
    default_root_url: String,
}

pub struct BuildModelResult {
    pub model: Option<Arc<ComponentModelForAnalyzer>>,
    pub errors: Vec<anyhow::Error>,
}

impl BuildModelResult {
    fn new() -> Self {
        Self { model: None, errors: Vec::new() }
    }
}

impl ModelBuilderForAnalyzer {
    pub fn new(default_root_url: impl Into<String>) -> Self {
        Self { default_root_url: default_root_url.into() }
    }

    pub async fn build(
        self,
        decls_by_url: HashMap<String, ComponentDecl>,
        runtime_config: Arc<RuntimeConfig>,
        component_id_index: Arc<ComponentIdIndex>,
        runner_registry: RunnerRegistry,
    ) -> BuildModelResult {
        let mut result = BuildModelResult::new();

        let root_url: String = match &runtime_config.root_component_url {
            Some(url) => url.clone().into(),
            None => self.default_root_url.clone().into(),
        };
        let build_tree_result = ComponentTreeBuilder::new(decls_by_url)
            .build(root_url, self.build_root_environment(&runtime_config, runner_registry));

        result.errors = build_tree_result
            .errors
            .into_iter()
            .map(|err| anyhow!("error building component instance tree: {}", err))
            .collect();

        if let Some(tree) = build_tree_result.tree {
            let mut model = ComponentModelForAnalyzer {
                top_instance: TopInstanceForAnalyzer::new(
                    runtime_config.namespace_capabilities.clone(),
                    runtime_config.builtin_capabilities.clone(),
                ),
                instances: HashMap::new(),
                policy_checker: GlobalPolicyChecker::new(Arc::clone(&runtime_config)),
                component_id_index,
            };

            match tree.get_root_node() {
                Ok(root) => {
                    if let Err(err) = self.build_realm(root, &tree, &mut model) {
                        result.errors.push(anyhow!(
                            "failed to build component model from instance tree: {}",
                            err
                        ));
                        return result;
                    }
                    result.model = Some(Arc::new(model));
                }
                Err(err) => {
                    result.errors.push(anyhow!(
                        "failed to retrieve root node from component instance tree: {}",
                        err
                    ));
                    return result;
                }
            }
        }

        result
    }

    // TODO(https://fxbug.dev/61861): This parallel implementation of component manager's builtin environment
    // setup will do for now, but is fragile and should be replaced soon. In particular, it doesn't provide a
    // way to register builtin runners or resolvers that appear in the `builtin_capabilities` field of the
    // RuntimeConfig but are not one of these hard-coded built-ins.
    fn build_root_environment(
        &self,
        runtime_config: &Arc<RuntimeConfig>,
        runner_registry: RunnerRegistry,
    ) -> NodeEnvironment {
        let mut resolver_registry = ResolverRegistry::default();

        // Register the boot resolver, if any
        match runtime_config.builtin_boot_resolver {
            component_internal::BuiltinBootResolver::Boot => {
                assert!(
                    resolver_registry
                        .register(&ResolverRegistration {
                            resolver: BOOT_RESOLVER_NAME.into(),
                            source: RegistrationSource::Self_,
                            scheme: BOOT_SCHEME.to_string(),
                        })
                        .is_none(),
                    "found duplicate resolver for boot scheme"
                );
            }
            component_internal::BuiltinBootResolver::Pkg => {
                assert!(
                    resolver_registry
                        .register(&ResolverRegistration {
                            resolver: PKG_RESOLVER_NAME.into(),
                            source: RegistrationSource::Self_,
                            scheme: PKG_SCHEME.to_string(),
                        })
                        .is_none(),
                    "found duplicate resolver for pkg scheme"
                );
            }
            component_internal::BuiltinBootResolver::None => {}
        };

        // Register the RealmBuilder resolver and runner, if any
        match runtime_config.realm_builder_resolver_and_runner {
            component_internal::RealmBuilderResolverAndRunner::Namespace => {
                assert!(
                    resolver_registry
                        .register(&ResolverRegistration {
                            resolver: REALM_BUILDER_RESOLVER_NAME.into(),
                            source: RegistrationSource::Self_,
                            scheme: REALM_BUILDER_SCHEME.to_string(),
                        })
                        .is_none(),
                    "found duplicate resolver for realm builder scheme"
                );
            }
            component_internal::RealmBuilderResolverAndRunner::None => {}
        }

        NodeEnvironment::new_root(runner_registry, resolver_registry, DebugRegistry::default())
    }

    fn build_realm(
        &self,
        node: &ComponentNode,
        tree: &ComponentTree,
        model: &mut ComponentModelForAnalyzer,
    ) -> anyhow::Result<Arc<ComponentInstanceForAnalyzer>> {
        let abs_moniker =
            AbsoluteMoniker::parse_string_without_instances(&node.node_path().to_string())
                .expect("failed to parse moniker from id");
        let parent = match node.parent() {
            Some(parent_id) => ExtendedInstanceInterface::Component(Arc::clone(
                model.instances.get(&parent_id).expect("parent instance not found"),
            )),
            None => ExtendedInstanceInterface::AboveRoot(Arc::clone(&model.top_instance)),
        };
        let environment = EnvironmentForAnalyzer::new(
            node.environment().clone(),
            WeakExtendedInstanceInterface::from(&parent),
        );
        let instance = Arc::new(ComponentInstanceForAnalyzer {
            abs_moniker,
            decl: node.decl.clone(),
            url: node.url().clone(),
            parent: WeakExtendedInstanceInterface::from(&parent),
            children: RwLock::new(HashMap::new()),
            environment,
            policy_checker: model.policy_checker.clone(),
            component_id_index: Arc::clone(&model.component_id_index),
        });
        model.instances.insert(node.node_path(), Arc::clone(&instance));
        self.build_children(node, tree, model)?;
        Ok(instance)
    }

    fn build_children(
        &self,
        node: &ComponentNode,
        tree: &ComponentTree,
        model: &mut ComponentModelForAnalyzer,
    ) -> anyhow::Result<()> {
        for child_id in node.children().iter() {
            let child_instance = self.build_realm(tree.get_node(child_id)?, tree, model)?;
            let partial_moniker = ChildMoniker::to_partial(
                child_instance
                    .abs_moniker()
                    .leaf()
                    .expect("expected child instance to have partial moniker"),
            );
            model
                .instances
                .get(&node.node_path())
                .expect("instance id not found")
                .children
                .write()
                .expect("failed to acquire write lock")
                .insert(partial_moniker, child_instance);
        }
        Ok(())
    }
}

/// `ComponentModelForAnalyzer` owns a representation of each v2 component instance and
/// supports lookup by `NodePath`.
pub struct ComponentModelForAnalyzer {
    top_instance: Arc<TopInstanceForAnalyzer>,
    instances: HashMap<NodePath, Arc<ComponentInstanceForAnalyzer>>,
    policy_checker: GlobalPolicyChecker,
    component_id_index: Arc<ComponentIdIndex>,
}

impl ComponentModelForAnalyzer {
    /// Returns the number of component instances in the model, not counting the top instance.
    pub fn len(&self) -> usize {
        self.instances.len()
    }

    /// Returns the component instance corresponding to `id` if it is present in the model, or an
    /// `InstanceNotFound` error if not.
    pub fn get_instance(
        self: &Arc<Self>,
        id: &NodePath,
    ) -> Result<Arc<ComponentInstanceForAnalyzer>, ComponentInstanceError> {
        let abs_moniker = AbsoluteMoniker::parse_string_without_instances(&id.to_string())
            .expect("failed to parse moniker from id");
        match self.instances.get(id) {
            Some(instance) => Ok(Arc::clone(instance)),
            None => Err(ComponentInstanceError::instance_not_found(abs_moniker.to_partial())),
        }
    }

    /// Given a `UseDecl` for a capability at an instance `target`, first routes the capability
    /// to its source and then validates the source.
    pub async fn check_use_capability(
        self: &Arc<Self>,
        use_decl: &UseDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<Vec<RouteMap>, AnalyzerModelError> {
        let (source, routes) = match use_decl.clone() {
            UseDecl::Directory(use_directory_decl) => {
                let (source, route) =
                    route_capability(RouteRequest::UseDirectory(use_directory_decl), target)
                        .await?;
                (source, vec![route])
            }
            UseDecl::Event(use_event_decl) => {
                if !self.uses_event_source_protocol(&target.decl) {
                    return Err(AnalyzerModelError::MissingEventSourceProtocol(
                        use_event_decl.target_name.to_string(),
                    ));
                }
                let (source, route) =
                    route_capability(RouteRequest::UseEvent(use_event_decl), target).await?;
                (source, vec![route])
            }
            UseDecl::Protocol(use_protocol_decl) => {
                let (source, route) =
                    route_capability(RouteRequest::UseProtocol(use_protocol_decl), target).await?;
                (source, vec![route])
            }
            UseDecl::Service(use_service_decl) => {
                let (source, route) =
                    route_capability(RouteRequest::UseService(use_service_decl), target).await?;
                (source, vec![route])
            }
            UseDecl::Storage(use_storage_decl) => {
                let (storage_source, _relative_moniker, storage_route, dir_route) =
                    route_storage_and_backing_directory(use_storage_decl, target).await?;
                (
                    RouteSource::StorageBackingDirectory(storage_source),
                    vec![storage_route, dir_route],
                )
            }
            _ => unimplemented![],
        };
        self.check_use_source(&source).await?;
        Ok(routes)
    }

    /// Given a `ExposeDecl` for a capability at an instance `target`, checks whether the capability
    /// can be used from an expose declaration. If so, routes the capability to its source and then
    /// validates the source.
    pub async fn check_use_exposed_capability(
        self: &Arc<Self>,
        expose_decl: &ExposeDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<Option<RouteMap>, AnalyzerModelError> {
        match self.request_from_expose(expose_decl) {
            Some(request) => {
                let (source, route) =
                    route_capability::<ComponentInstanceForAnalyzer>(request, target).await?;
                self.check_use_source(&source).await?;
                Ok(Some(route))
            }
            None => Ok(None),
        }
    }

    /// Given a `ProgramDecl` for a component instance, checks whether the specified runner has
    /// a valid capability route.
    pub async fn check_program_runner(
        self: &Arc<Self>,
        program_decl: &ProgramDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<Option<RouteMap>, AnalyzerModelError> {
        match program_decl.runner {
            Some(ref runner) => {
                let mut route = RouteMap::from_segments(vec![RouteSegment::RequireRunner {
                    node_path: target.node_path(),
                    runner: runner.clone(),
                }]);
                let (_source, mut segments) = route_capability::<ComponentInstanceForAnalyzer>(
                    RouteRequest::Runner(runner.clone()),
                    target,
                )
                .await?;
                route.append(&mut segments);
                Ok(Some(route))
            }
            None => Ok(None),
        }
    }

    /// Given a component instance, extracts the URL scheme of each child of that instance. Then
    /// checks that each scheme corresponds to a unique resolver in the component's scope, and
    /// checks that each resolver has a valid capability route. Returns the results of those checks
    /// in sorted order by URL scheme name. If any URL parsing errors occurred, they appear at the
    /// beginning of the vector of results.
    pub async fn check_child_resolvers(
        self: &Arc<Self>,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Vec<Result<RouteMap, AnalyzerModelError>> {
        let mut results = Vec::new();
        // Results of checks so far, keyed by URL scheme.
        let mut checked = HashMap::new();
        for child in target.decl.children.iter() {
            match Self::get_absolute_child_url(&child.url, target) {
                Ok(absolute_url) => {
                    let scheme = absolute_url.scheme();
                    if checked.get(scheme).is_none() {
                        let mut route =
                            RouteMap::from_segments(vec![RouteSegment::RequireResolver {
                                node_path: target.node_path(),
                                scheme: scheme.to_string(),
                            }]);
                        let check_result = match self
                            .check_resolver_for_scheme(scheme, &child.environment, target)
                            .await
                        {
                            Ok(mut segments) => {
                                route.append(&mut segments);
                                Ok(route)
                            }
                            Err(err) => Err(err),
                        };
                        checked.insert(scheme.to_string(), check_result);
                    }
                }
                Err(err) => {
                    results.push(Err(err));
                }
            }
        }
        let mut results_by_scheme =
            checked.drain().collect::<Vec<(String, Result<RouteMap, AnalyzerModelError>)>>();
        results_by_scheme.sort_by(|x, y| x.0.cmp(&y.0));

        results.append(&mut results_by_scheme.into_iter().map(|(_, route)| route).collect());
        results
    }

    /// Looks for a resolver for `scheme` in one of two places. If `environment` contains an
    /// environment name, looks first for an environment defined by `target` with that name
    /// and attempts to find the resolver there. If not found, or if `environment` is None,
    /// looks for the resolver in the environment offered to `target`.
    ///
    /// After finding a resolver, checks that it is routed correctly.
    async fn check_resolver_for_scheme(
        self: &Arc<Self>,
        scheme: &str,
        environment: &Option<String>,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<RouteMap, AnalyzerModelError> {
        // The child was declared with a named environment. A resolver for the child's url
        // scheme can be sourced from that environment, if a matching resolver is present.
        if let Some(env_name) = environment {
            if let Some(env) = target.decl.environments.iter().find(|e| &e.name == env_name) {
                if let Some(resolver) = env.resolvers.iter().find(|r| r.scheme == scheme) {
                    let (_source, route) = route_capability::<ComponentInstanceForAnalyzer>(
                        RouteRequest::Resolver(resolver.clone()),
                        target,
                    )
                    .await?;
                    return Ok(route);
                }
            }
        }
        // Either the child was not declared with a named environment, or that environment
        // did not provide a matching resolver. The resolver, if any, must be available in
        // `target`'s own environment.
        match target.environment.get_registered_resolver(scheme)? {
            Some((ExtendedInstanceInterface::Component(instance), resolver)) => {
                let (_source, route) = route_capability::<ComponentInstanceForAnalyzer>(
                    RouteRequest::Resolver(resolver),
                    &instance,
                )
                .await?;
                Ok(route)
            }
            Some((ExtendedInstanceInterface::AboveRoot(_), resolver)) => {
                let decl = self.get_builtin_resolver_decl(&resolver)?;
                let mut route = RouteMap::new();
                route.push(RouteSegment::ProvideAsBuiltin { capability: decl });
                Ok(route)
            }
            None => Err(AnalyzerModelError::MissingResolverForScheme(scheme.to_string())),
        }
    }

    // Retrieves the `CapabilityDecl` for a built-in resolver from its registration, or an
    // error if the resolver is not provided as a built-in capability.
    fn get_builtin_resolver_decl(
        &self,
        resolver: &ResolverRegistration,
    ) -> Result<CapabilityDecl, AnalyzerModelError> {
        match self.top_instance.builtin_capabilities().iter().find(|&decl| {
            if let CapabilityDecl::Resolver(resolver_decl) = decl {
                resolver_decl.name == resolver.resolver
            } else {
                false
            }
        }) {
            Some(decl) => Ok(decl.clone()),
            None => Err(AnalyzerModelError::RoutingError(
                RoutingError::use_from_component_manager_not_found(resolver.resolver.to_string()),
            )),
        }
    }

    // Given a component instance `target` and the url `child_url` of a child of that instance,
    // returns an absolute url for the child.
    fn get_absolute_child_url(
        child_url: &str,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<Url, AnalyzerModelError> {
        match Url::parse(&child_url) {
            Ok(url) => Ok(url),
            Err(url::ParseError::RelativeUrlWithoutBase) => {
                let absolute_prefix = match component_has_relative_url(target) {
                    true => find_first_absolute_ancestor_url(target),
                    false => {
                        Url::parse(target.url()).map_err(|_| ComponentInstanceError::MalformedUrl {
                            url: target.url().to_string(),
                            moniker: target.abs_moniker().to_partial(),
                        })
                    }
                }?;
                Ok(absolute_prefix.join(child_url).unwrap())
            }
            _ => Err(ComponentInstanceError::MalformedUrl {
                url: target.url().to_string(),
                moniker: target.abs_moniker().to_partial(),
            }
            .into()),
        }
    }

    /// Checks properties of a capability source that are necessary to use the capability
    /// and that are possible to verify statically.
    async fn check_use_source(
        &self,
        route_source: &RouteSource<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match route_source {
            RouteSource::Directory(source, _) => self.check_directory_source(source).await,
            RouteSource::Event(_) => Ok(()),
            RouteSource::Protocol(source) => self.check_protocol_source(source).await,
            RouteSource::Service(source) => self.check_service_source(source).await,
            RouteSource::StorageBackingDirectory(source) => self.check_storage_source(source).await,
            _ => unimplemented![],
        }
    }

    /// If the source of a directory capability is a component instance, checks that that
    /// instance is executable.
    async fn check_directory_source(
        &self,
        source: &CapabilitySourceInterface<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match source {
            CapabilitySourceInterface::Component { component: weak, .. } => {
                self.check_executable(&weak.upgrade()?).await
            }
            CapabilitySourceInterface::Namespace { .. } => Ok(()),
            _ => unimplemented![],
        }
    }

    /// If the source of a protocol capability is a component instance, checks that that
    /// instance is executable.
    ///
    /// If the source is a capability, checks that the protocol is the `StorageAdmin`
    /// protocol and that the source is a valid storage capability.
    async fn check_protocol_source(
        &self,
        source: &CapabilitySourceInterface<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match source {
            CapabilitySourceInterface::Component { component: weak, .. } => {
                self.check_executable(&weak.upgrade()?).await
            }
            CapabilitySourceInterface::Namespace { .. } => Ok(()),
            CapabilitySourceInterface::Capability { source_capability, component: weak } => {
                self.check_protocol_capability_source(&weak.upgrade()?, &source_capability).await
            }
            CapabilitySourceInterface::Builtin { .. } => Ok(()),
            _ => unimplemented![],
        }
    }

    // A helper function validating a source of type `Capability` for a protocol capability.
    async fn check_protocol_capability_source(
        &self,
        source_component: &Arc<ComponentInstanceForAnalyzer>,
        source_capability: &ComponentCapability,
    ) -> Result<(), AnalyzerModelError> {
        let source_capability_name = source_capability
            .source_capability_name()
            .expect("failed to get source capability name");

        match source_capability.source_name().map(|name| name.to_string()).as_deref() {
            Some(fsys::StorageAdminMarker::NAME) => {
                match source_component.decl.find_storage_source(source_capability_name) {
                    Some(_) => Ok(()),
                    None => Err(AnalyzerModelError::InvalidSourceCapability(
                        source_capability_name.to_string(),
                        fsys::StorageAdminMarker::NAME.to_string(),
                    )),
                }
            }
            _ => Err(AnalyzerModelError::InvalidSourceCapability(
                source_capability_name.to_string(),
                source_capability
                    .source_name()
                    .map_or_else(|| "".to_string(), |name| name.to_string()),
            )),
        }
    }

    /// If the source of a service capability is a component instance, checks that that
    /// instance is executable.
    async fn check_service_source(
        &self,
        source: &CapabilitySourceInterface<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match source {
            CapabilitySourceInterface::Component { component: weak, .. } => {
                self.check_executable(&weak.upgrade()?).await
            }
            CapabilitySourceInterface::Namespace { .. } => Ok(()),
            _ => unimplemented![],
        }
    }

    /// If the source of a storage backing directory is a component instance, checks that that
    /// instance is executable.
    async fn check_storage_source(
        &self,
        source: &StorageCapabilitySource<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        if let Some(provider) = &source.storage_provider {
            self.check_executable(provider).await?
        }
        Ok(())
    }

    // A helper function which prepares a route request for capabilities which can be used
    // from an expose declaration, and returns None if the capability type cannot be used
    // from an expose.
    fn request_from_expose(self: &Arc<Self>, expose_decl: &ExposeDecl) -> Option<RouteRequest> {
        match expose_decl {
            ExposeDecl::Directory(expose_directory_decl) => {
                Some(RouteRequest::ExposeDirectory(expose_directory_decl.clone()))
            }
            ExposeDecl::Protocol(expose_protocol_decl) => {
                Some(RouteRequest::ExposeProtocol(expose_protocol_decl.clone()))
            }
            ExposeDecl::Service(expose_service_decl) => {
                Some(RouteRequest::ExposeService(expose_service_decl.clone()))
            }
            _ => None,
        }
    }

    // A helper function checking whether a component instance is executable.
    async fn check_executable(
        &self,
        component: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match component.decl.program {
            Some(_) => Ok(()),
            None => Err(AnalyzerModelError::SourceInstanceNotExecutable(
                component.abs_moniker().to_string(),
            )),
        }
    }

    fn uses_event_source_protocol(&self, decl: &ComponentDecl) -> bool {
        decl.uses.iter().any(|u| match u {
            UseDecl::Protocol(p) => {
                p.target_path
                    == CapabilityPath {
                        dirname: "/svc".to_string(),
                        basename: "fuchsia.sys2.EventSource".to_string(),
                    }
            }
            _ => false,
        })
    }
}

/// A representation of a v2 component instance.
#[derive(Debug)]
pub struct ComponentInstanceForAnalyzer {
    abs_moniker: AbsoluteMoniker,
    decl: ComponentDecl,
    url: String,
    parent: WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer>,
    children: RwLock<HashMap<PartialChildMoniker, Arc<ComponentInstanceForAnalyzer>>>,
    environment: Arc<EnvironmentForAnalyzer>,
    policy_checker: GlobalPolicyChecker,
    component_id_index: Arc<ComponentIdIndex>,
}

impl ComponentInstanceForAnalyzer {
    /// Exposes the component's ComponentDecl. This is referenced directly in
    /// tests.
    pub fn decl_for_testing(&self) -> &ComponentDecl {
        &self.decl
    }

    fn node_path(&self) -> NodePath {
        NodePath::absolute_from_vec(
            self.abs_moniker().to_partial().path().into_iter().map(|m| m.as_str()).collect(),
        )
    }
}

/// A representation of `ComponentManager`'s instance, providing a set of capabilities to
/// the root component instance.
#[derive(Debug)]
pub struct TopInstanceForAnalyzer {
    namespace_capabilities: NamespaceCapabilities,
    builtin_capabilities: BuiltinCapabilities,
}

/// A representation of a capability route.
#[derive(Clone, Debug, PartialEq)]
pub struct RouteMap(Vec<RouteSegment>);

impl RouteMap {
    pub fn new() -> Self {
        RouteMap(Vec::new())
    }

    pub fn from_segments(segments: Vec<RouteSegment>) -> Self {
        RouteMap(segments)
    }

    pub fn push(&mut self, segment: RouteSegment) {
        self.0.push(segment)
    }

    pub fn append(&mut self, other: &mut Self) {
        self.0.append(&mut other.0)
    }
}

/// A struct implementing `DebugRouteMapper` that records a `RouteMap` as the router
/// walks a capability route.
#[derive(Clone, Debug)]
pub struct RouteMapper {
    route: RouteMap,
}

impl DebugRouteMapper for RouteMapper {
    type RouteMap = RouteMap;

    fn add_use(&mut self, abs_moniker: AbsoluteMoniker, use_decl: UseDecl) {
        self.route.push(RouteSegment::UseBy {
            node_path: NodePath::from(abs_moniker),
            capability: use_decl,
        })
    }

    fn add_offer(&mut self, abs_moniker: AbsoluteMoniker, offer_decl: OfferDecl) {
        self.route.push(RouteSegment::OfferBy {
            node_path: NodePath::from(abs_moniker),
            capability: offer_decl,
        })
    }

    fn add_expose(&mut self, abs_moniker: AbsoluteMoniker, expose_decl: ExposeDecl) {
        self.route.push(RouteSegment::ExposeBy {
            node_path: NodePath::from(abs_moniker),
            capability: expose_decl,
        })
    }

    fn add_registration(
        &mut self,
        abs_moniker: AbsoluteMoniker,
        registration_decl: RegistrationDecl,
    ) {
        self.route.push(RouteSegment::RegisterBy {
            node_path: NodePath::from(abs_moniker),
            capability: registration_decl,
        })
    }

    fn add_component_capability(
        &mut self,
        abs_moniker: AbsoluteMoniker,
        capability_decl: CapabilityDecl,
    ) {
        self.route.push(RouteSegment::DeclareBy {
            node_path: NodePath::from(abs_moniker),
            capability: capability_decl,
        })
    }

    fn add_framework_capability(&mut self, capability_name: CapabilityName) {
        self.route.push(RouteSegment::ProvideFromFramework { capability: capability_name })
    }

    fn add_builtin_capability(&mut self, capability_decl: CapabilityDecl) {
        self.route.push(RouteSegment::ProvideAsBuiltin { capability: capability_decl })
    }

    fn add_namespace_capability(&mut self, capability_decl: CapabilityDecl) {
        self.route.push(RouteSegment::ProvideFromNamespace { capability: capability_decl })
    }

    fn get_route(self) -> RouteMap {
        self.route
    }
}

#[async_trait]
impl ComponentInstanceInterface for ComponentInstanceForAnalyzer {
    type TopInstance = TopInstanceForAnalyzer;
    type DebugRouteMapper = RouteMapper;

    fn abs_moniker(&self) -> &AbsoluteMoniker {
        &self.abs_moniker
    }

    fn url(&self) -> &str {
        &self.url
    }

    fn environment(&self) -> &dyn EnvironmentInterface<Self> {
        self.environment.as_ref()
    }

    fn try_get_parent(&self) -> Result<ExtendedInstanceInterface<Self>, ComponentInstanceError> {
        Ok(self.parent.upgrade()?)
    }

    fn try_get_policy_checker(&self) -> Result<GlobalPolicyChecker, ComponentInstanceError> {
        Ok(self.policy_checker.clone())
    }

    fn try_get_component_id_index(&self) -> Result<Arc<ComponentIdIndex>, ComponentInstanceError> {
        Ok(Arc::clone(&self.component_id_index))
    }

    async fn lock_resolved_state<'a>(
        self: &'a Arc<Self>,
    ) -> Result<
        Box<dyn ResolvedInstanceInterface<Component = ComponentInstanceForAnalyzer> + 'a>,
        ComponentInstanceError,
    > {
        Ok(Box::new(&**self))
    }

    fn new_route_mapper() -> RouteMapper {
        RouteMapper { route: RouteMap::new() }
    }
}

impl ResolvedInstanceInterface for ComponentInstanceForAnalyzer {
    type Component = ComponentInstanceForAnalyzer;

    fn uses(&self) -> Vec<UseDecl> {
        self.decl.uses.clone()
    }

    fn exposes(&self) -> Vec<ExposeDecl> {
        self.decl.exposes.clone()
    }

    fn offers(&self) -> Vec<OfferDecl> {
        self.decl.offers.clone()
    }

    fn capabilities(&self) -> Vec<CapabilityDecl> {
        self.decl.capabilities.clone()
    }

    fn collections(&self) -> Vec<CollectionDecl> {
        self.decl.collections.clone()
    }

    fn get_live_child(
        &self,
        moniker: &PartialChildMoniker,
    ) -> Option<Arc<ComponentInstanceForAnalyzer>> {
        self.children.read().expect("failed to acquire read lock").get(moniker).map(Arc::clone)
    }

    // This is a static model with no notion of a collection.
    fn live_children_in_collection(
        &self,
        _collection: &str,
    ) -> Vec<(PartialChildMoniker, Arc<ComponentInstanceForAnalyzer>)> {
        vec![]
    }
}

impl TopInstanceForAnalyzer {
    fn new(
        namespace_capabilities: NamespaceCapabilities,
        builtin_capabilities: BuiltinCapabilities,
    ) -> Arc<Self> {
        Arc::new(Self { namespace_capabilities, builtin_capabilities })
    }
}

impl TopInstanceInterface for TopInstanceForAnalyzer {
    fn namespace_capabilities(&self) -> &NamespaceCapabilities {
        &self.namespace_capabilities
    }

    fn builtin_capabilities(&self) -> &BuiltinCapabilities {
        &self.builtin_capabilities
    }
}

/// A representation of a v2 component instance's environment and its relationship to the
/// parent realm's environment.
#[derive(Debug)]
pub struct EnvironmentForAnalyzer {
    environment: NodeEnvironment,
    parent: WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer>,
}

impl EnvironmentForAnalyzer {
    fn new(
        environment: NodeEnvironment,
        parent: WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer>,
    ) -> Arc<Self> {
        Arc::new(Self { environment, parent })
    }

    /// Returns the resolver registered for `scheme` and the component that created the environment the
    /// resolver was registered to. Returns `None` if there was no match.
    #[allow(dead_code)]
    fn get_registered_resolver(
        &self,
        scheme: &str,
    ) -> Result<
        Option<(ExtendedInstanceInterface<ComponentInstanceForAnalyzer>, ResolverRegistration)>,
        ComponentInstanceError,
    > {
        let parent = self.parent().upgrade()?;
        match self.resolver_registry().get_resolver(scheme) {
            Some(reg) => Ok(Some((parent, reg.clone()))),
            None => match self.extends() {
                EnvironmentExtends::Realm => match parent {
                    ExtendedInstanceInterface::Component(parent) => {
                        parent.environment.get_registered_resolver(scheme)
                    }
                    ExtendedInstanceInterface::AboveRoot(_) => {
                        unreachable!("root env can't extend")
                    }
                },
                EnvironmentExtends::None => {
                    return Ok(None);
                }
            },
        }
    }

    #[allow(dead_code)]
    fn resolver_registry(&self) -> &ResolverRegistry {
        &self.environment.resolver_registry()
    }
}

impl EnvironmentInterface<ComponentInstanceForAnalyzer> for EnvironmentForAnalyzer {
    fn name(&self) -> Option<&str> {
        self.environment.name()
    }

    fn parent(&self) -> &WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer> {
        &self.parent
    }

    fn extends(&self) -> &EnvironmentExtends {
        self.environment.extends()
    }

    fn runner_registry(&self) -> &RunnerRegistry {
        self.environment.runner_registry()
    }

    fn debug_registry(&self) -> &DebugRegistry {
        self.environment.debug_registry()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::capability_routing::testing::*,
        anyhow::Result,
        cm_rust::{EnvironmentDecl, RegistrationSource, RunnerRegistration},
        futures::executor::block_on,
    };

    // Builds a model from a 2-node `ComponentTree` with structure `root -- child`, retrieves
    // each of the 2 resulting component instances, and tests their public methods.
    #[test]
    fn build_model() -> Result<()> {
        let root_url = "root_url".to_string();
        let child_url = "child_url".to_string();
        let child_name = "child".to_string();

        let root_decl = new_component_decl(
            vec![],
            vec![],
            vec![],
            vec![],
            vec![new_child_decl(&child_name, &child_url)],
        );
        let child_decl = new_component_decl(vec![], vec![], vec![], vec![], vec![]);

        let mut decls = HashMap::new();
        decls.insert(root_url.clone(), root_decl.clone());
        decls.insert(child_url, child_decl.clone());

        let config = Arc::new(RuntimeConfig::default());
        let build_model_result = block_on(async {
            ModelBuilderForAnalyzer::new(root_url.clone())
                .build(
                    decls,
                    config,
                    Arc::new(ComponentIdIndex::default()),
                    RunnerRegistry::default(),
                )
                .await
        });
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 2);

        let child_moniker = PartialChildMoniker::new("child".to_string(), None);
        let root_id = NodePath::new(vec![]);
        let child_id = root_id.extended(child_moniker.clone());
        let other_id = root_id.extended(PartialChildMoniker::new("other".to_string(), None));

        let root_instance = model.get_instance(&root_id)?;
        let child_instance = model.get_instance(&child_id)?;

        let get_other_result = model.get_instance(&other_id);
        assert_eq!(
            get_other_result.err().unwrap().to_string(),
            ComponentInstanceError::instance_not_found(
                AbsoluteMoniker::parse_string_without_instances(&other_id.to_string())
                    .expect("failed to parse moniker from id")
                    .to_partial()
            )
            .to_string()
        );

        assert_eq!(root_instance.abs_moniker(), &AbsoluteMoniker::root());
        assert_eq!(
            child_instance.abs_moniker(),
            &AbsoluteMoniker::parse_string_without_instances("/child")
                .expect("failed to parse moniker from id")
        );

        match root_instance.try_get_parent()? {
            ExtendedInstanceInterface::AboveRoot(_) => {}
            _ => panic!("root instance's parent should be `AboveRoot`"),
        }
        match child_instance.try_get_parent()? {
            ExtendedInstanceInterface::Component(component) => {
                assert_eq!(component.abs_moniker(), root_instance.abs_moniker())
            }
            _ => panic!("child instance's parent should be root component"),
        }

        let get_child = block_on(async {
            root_instance
                .lock_resolved_state()
                .await
                .map(|locked| locked.get_live_child(&child_moniker))
        })?;
        assert!(get_child.is_some());
        assert_eq!(get_child.unwrap().abs_moniker(), child_instance.abs_moniker());

        let root_environment = root_instance.environment();
        let child_environment = child_instance.environment();

        assert_eq!(root_environment.name(), None);
        match root_environment.parent() {
            WeakExtendedInstanceInterface::AboveRoot(_) => {}
            _ => panic!("root environment's parent should be `AboveRoot`"),
        }

        assert_eq!(child_environment.name(), None);
        match child_environment.parent() {
            WeakExtendedInstanceInterface::Component(component) => {
                assert_eq!(component.upgrade()?.abs_moniker(), root_instance.abs_moniker())
            }
            _ => panic!("child environment's parent should be root component"),
        }

        root_instance.try_get_policy_checker()?;
        root_instance.try_get_component_id_index()?;

        child_instance.try_get_policy_checker()?;
        child_instance.try_get_component_id_index()?;

        block_on(async {
            assert!(root_instance.lock_resolved_state().await.is_ok());
            assert!(child_instance.lock_resolved_state().await.is_ok());
        });

        Ok(())
    }

    // Builds a model from a 2-node `ComponentTree` with structure `root -- child` in which the child
    // environment extends the root's. Checks that the child has access to the inherited runner and
    // resolver registrations through its environment.
    #[test]
    fn environment_inherits() -> Result<()> {
        let root_url = "root_url".to_string();
        let child_url = "child_url".to_string();
        let child_name = "child".to_string();
        let child_env_name = "child_env".to_string();

        let child_runner_registration = RunnerRegistration {
            source_name: "child_env_runner".into(),
            source: RegistrationSource::Self_,
            target_name: "child_env_runner".into(),
        };
        let child_resolver_registration = ResolverRegistration {
            resolver: "child_env_resolver".into(),
            source: RegistrationSource::Self_,
            scheme: "child_resolver_scheme".into(),
        };
        let child_env_decl = EnvironmentDecl {
            name: child_env_name.clone(),
            extends: fsys::EnvironmentExtends::Realm,
            runners: vec![child_runner_registration.clone()],
            resolvers: vec![child_resolver_registration.clone()],
            debug_capabilities: vec![],
            stop_timeout_ms: None,
        };

        let mut child_decl = new_child_decl(&child_name, &child_url);
        child_decl.environment = Some(child_env_name.clone());
        let mut root_decl = new_component_decl(vec![], vec![], vec![], vec![], vec![child_decl]);
        root_decl.environments.push(child_env_decl);

        let mut decls = HashMap::new();
        decls.insert(root_url.clone(), root_decl.clone());
        decls.insert(child_url, new_component_decl(vec![], vec![], vec![], vec![], vec![]));

        // Set up the RuntimeConfig to register the `fuchsia-boot` resolver as a built-in,
        // in addition to `builtin_runner`.
        let mut config = RuntimeConfig::default();
        config.builtin_boot_resolver = component_internal::BuiltinBootResolver::Boot;

        let builtin_runner_name = CapabilityName("builtin_runner".into());
        let builtin_runner_registration = RunnerRegistration {
            source_name: builtin_runner_name.clone(),
            source: RegistrationSource::Self_,
            target_name: builtin_runner_name.clone(),
        };

        let build_model_result = block_on(async {
            ModelBuilderForAnalyzer::new(root_url.clone())
                .build(
                    decls,
                    Arc::new(config),
                    Arc::new(ComponentIdIndex::default()),
                    RunnerRegistry::from_decl(&vec![builtin_runner_registration]),
                )
                .await
        });
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 2);

        let child_instance = model.get_instance(&NodePath::absolute_from_vec(vec![&child_name]))?;

        let get_child_runner_result = child_instance
            .environment
            .get_registered_runner(&child_runner_registration.target_name)?;
        assert!(get_child_runner_result.is_some());
        let (child_runner_registrar, child_runner) = get_child_runner_result.unwrap();
        match child_runner_registrar {
            ExtendedInstanceInterface::Component(instance) => {
                assert_eq!(instance.abs_moniker(), &AbsoluteMoniker::from(vec![]));
            }
            ExtendedInstanceInterface::AboveRoot(_) => {
                panic!("expected child_env_runner to be registered by the root instance")
            }
        }
        assert_eq!(child_runner_registration, child_runner);

        let get_child_resolver_result = child_instance
            .environment
            .get_registered_resolver(&child_resolver_registration.scheme)?;
        assert!(get_child_resolver_result.is_some());
        let (child_resolver_registrar, child_resolver) = get_child_resolver_result.unwrap();
        match child_resolver_registrar {
            ExtendedInstanceInterface::Component(instance) => {
                assert_eq!(instance.abs_moniker(), &AbsoluteMoniker::from(vec![]));
            }
            ExtendedInstanceInterface::AboveRoot(_) => {
                panic!("expected child_env_resolver to be registered by the root instance")
            }
        }
        assert_eq!(child_resolver_registration, child_resolver);

        let get_builtin_runner_result = child_instance
            .environment
            .get_registered_runner(&CapabilityName::from(builtin_runner_name))?;
        assert!(get_builtin_runner_result.is_some());
        let (builtin_runner_registrar, _builtin_runner) = get_builtin_runner_result.unwrap();
        match builtin_runner_registrar {
            ExtendedInstanceInterface::Component(_) => {
                panic!("expected builtin runner to be registered above the root")
            }
            ExtendedInstanceInterface::AboveRoot(_) => {}
        }

        let get_builtin_resolver_result =
            child_instance.environment.get_registered_resolver(&BOOT_SCHEME.to_string())?;
        assert!(get_builtin_resolver_result.is_some());
        let (builtin_resolver_registrar, _builtin_resolver) = get_builtin_resolver_result.unwrap();
        match builtin_resolver_registrar {
            ExtendedInstanceInterface::Component(_) => {
                panic!("expected boot resolver to be registered above the root")
            }
            ExtendedInstanceInterface::AboveRoot(_) => {}
        }

        Ok(())
    }
}
