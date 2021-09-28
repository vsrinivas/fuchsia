// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_routing::route::RouteSegment,
        component_tree::{ComponentNode, ComponentTree, NodeEnvironment, NodePath},
    },
    async_trait::async_trait,
    cm_rust::{
        CapabilityDecl, CapabilityName, CapabilityPath, CollectionDecl, ComponentDecl, ExposeDecl,
        OfferDecl, ProgramDecl, UseDecl,
    },
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon_status as zx_status,
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
        environment::{DebugRegistry, EnvironmentExtends, EnvironmentInterface, RunnerRegistry},
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
};

#[derive(Debug, Error, Deserialize, Serialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum AnalyzerModelError {
    #[error("the source instance `{0}` is not executable")]
    SourceInstanceNotExecutable(String),

    #[error("the capability `{0}` is not a valid source for the capability `{1}`")]
    InvalidSourceCapability(String, String),

    #[error("uses Event capability `{0}` without using the EventSource protocol")]
    MissingEventSourceProtocol(String),

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
            Self::ComponentInstanceError(err) => err.as_zx_status(),
            Self::RoutingError(err) => err.as_zx_status(),
        }
    }
}

/// Builds a `ComponentModelForAnalyzer` from a `ComponentTree` and a `RuntimeConfig`.
pub struct ModelBuilderForAnalyzer {}

impl ModelBuilderForAnalyzer {
    pub fn new() -> Self {
        Self {}
    }

    pub async fn build(
        self,
        tree: ComponentTree,
        runtime_config: Arc<RuntimeConfig>,
    ) -> anyhow::Result<Arc<ComponentModelForAnalyzer>> {
        let mut model = ComponentModelForAnalyzer {
            top_instance: TopInstanceForAnalyzer::new(
                runtime_config.namespace_capabilities.clone(),
                runtime_config.builtin_capabilities.clone(),
            ),
            instances: HashMap::new(),
            policy_checker: GlobalPolicyChecker::new(Arc::clone(&runtime_config)),
            component_id_index: Arc::new(ComponentIdIndex::default()),
        };
        if let Some(ref index_path) = runtime_config.component_id_index_path {
            model.component_id_index = Arc::new(ComponentIdIndex::new(index_path).await?);
        }
        let root = tree.get_root_node()?;
        self.build_realm(root, &tree, &mut model)?;
        Ok(Arc::new(model))
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

/// `ComponentModelForAnalzyer` owns a representation of each v2 component instance and
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
            Some(ref runner_name) => {
                match route_capability::<ComponentInstanceForAnalyzer>(
                    RouteRequest::Runner(runner_name.clone()),
                    target,
                )
                .await
                {
                    Ok((_source, route)) => Ok(Some(route)),
                    Err(err) => Err(AnalyzerModelError::RoutingError(err)),
                }
            }
            None => Ok(None),
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
        super::*, crate::capability_routing::testing::build_two_node_tree, anyhow::Result,
        futures::executor::block_on,
    };

    // Builds a model from a 2-node `ComponentTree` with structure `root -- child`, retrieves
    // each of the 2 resulting component instances, and tests their public methods.
    #[test]
    fn build_model() -> Result<()> {
        let tree =
            build_two_node_tree(vec![], vec![], vec![], vec![], vec![], vec![]).tree.unwrap();
        let config = Arc::new(RuntimeConfig::default());
        let model = block_on(async { ModelBuilderForAnalyzer::new().build(tree, config).await })?;
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
}
