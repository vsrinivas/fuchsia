// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        component_instance::{ComponentInstanceForAnalyzer, TopInstanceForAnalyzer},
        match_absolute_component_urls,
        node_path::NodePath,
        route::{RouteMap, RouteSegment, VerifyRouteResult},
        PkgUrlMatch,
    },
    anyhow::{anyhow, Context, Result},
    cm_moniker::InstancedRelativeMoniker,
    cm_rust::{
        CapabilityDecl, CapabilityPath, CapabilityTypeName, ComponentDecl, ExposeDecl,
        ExposeDeclCommon, ProgramDecl, ResolverRegistration, UseDecl, UseEventStreamDecl,
        UseStorageDecl,
    },
    config_encoder::ConfigFields,
    fidl::prelude::*,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::AbsoluteComponentUrl,
    fuchsia_zircon_status as zx_status,
    futures::FutureExt,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase},
    routing::{
        capability_source::{
            CapabilitySourceInterface, ComponentCapability, StorageCapabilitySource,
        },
        component_id_index::ComponentIdIndex,
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, TopInstanceInterface,
        },
        config::RuntimeConfig,
        environment::{
            component_has_relative_url, find_first_absolute_ancestor_url, RunnerRegistry,
        },
        error::{AvailabilityRoutingError, ComponentInstanceError, RoutingError},
        policy::GlobalPolicyChecker,
        route_capability, route_event_stream_capability, route_storage_and_backing_directory,
        DebugRouteMapper, RouteRequest, RouteSource,
    },
    serde::{Deserialize, Serialize},
    std::{
        collections::{BTreeMap, HashMap, HashSet},
        sync::Arc,
    },
    thiserror::Error,
    url::Url,
};

/// Errors that may occur when building a `ComponentModelForAnalyzer` from
/// a set of component manifests.
#[derive(Clone, Debug, Deserialize, Error, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum BuildAnalyzerModelError {
    #[error("no component declaration found for url `{0}` requested by node `{1}`")]
    ComponentDeclNotFound(String, String),

    #[error("invalid child declaration containing url `{0}` at node `{1}`")]
    InvalidChildDecl(String, String),

    #[error("no node found with path `{0}`")]
    ComponentNodeNotFound(String),

    #[error("environment `{0}` requested by child `{1}` not found at node `{2}`")]
    EnvironmentNotFound(String, String, String),

    #[error("multiple resolvers found for scheme `{0}`")]
    DuplicateResolverScheme(String),

    #[error("malformed url {0} for component instance {1}")]
    MalformedUrl(String, String),

    #[error("dynamic component with url {0} an invalid moniker")]
    DynamicComponentInvalidMoniker(String),

    #[error("dynamic component at {0} with url {1} is not part of a collection")]
    DynamicComponentWithoutCollection(String, String),
}

/// Errors that a `ComponentModelForAnalyzer` may detect in the component graph.
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

/// Builds a `ComponentModelForAnalyzer` from a set of component manifests.
pub struct ModelBuilderForAnalyzer {
    default_root_url: Url,
}

/// The type returned by `ModelBuilderForAnalyzer::build()`. May contain some
/// errors even if `model` is `Some`.
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
    pub fn new(default_root_url: Url) -> Self {
        Self { default_root_url }
    }

    fn load_dynamic_components(
        input: HashMap<NodePath, (AbsoluteComponentUrl, Option<String>)>,
    ) -> (HashMap<AbsoluteMoniker, Vec<Child>>, Vec<anyhow::Error>) {
        let mut errors: Vec<anyhow::Error> = vec![];
        let mut dynamic_components: HashMap<AbsoluteMoniker, Vec<Child>> = HashMap::new();
        for (node_path, (url, environment)) in input.into_iter() {
            let mut moniker_vec = node_path.as_vec();
            let child_moniker_str = moniker_vec.pop();
            if child_moniker_str.is_none() {
                errors.push(
                    BuildAnalyzerModelError::DynamicComponentInvalidMoniker(url.to_string()).into(),
                );
                continue;
            }
            let child_moniker_str = child_moniker_str.unwrap();

            let abs_moniker: AbsoluteMoniker = moniker_vec.into();
            let child_moniker: ChildMoniker = child_moniker_str.into();
            if child_moniker.collection().is_none() {
                errors.push(
                    BuildAnalyzerModelError::DynamicComponentWithoutCollection(
                        node_path.to_string(),
                        url.to_string(),
                    )
                    .into(),
                );
                continue;
            }

            let children = dynamic_components.entry(abs_moniker.clone()).or_insert_with(|| vec![]);
            match Url::parse(&url.to_string()) {
                Ok(url) => {
                    children.push(Child { child_moniker, url, environment });
                }
                Err(_) => {
                    let node_path: NodePath = abs_moniker.into();
                    errors.push(
                        BuildAnalyzerModelError::MalformedUrl(
                            url.to_string(),
                            node_path.to_string(),
                        )
                        .into(),
                    );
                }
            }
        }
        (dynamic_components, errors)
    }

    pub fn build(
        self,
        decls_by_url: HashMap<Url, (ComponentDecl, Option<ConfigFields>)>,
        runtime_config: Arc<RuntimeConfig>,
        component_id_index: Arc<ComponentIdIndex>,
        runner_registry: RunnerRegistry,
    ) -> BuildModelResult {
        self.build_with_dynamic_components(
            HashMap::new(),
            decls_by_url,
            runtime_config,
            component_id_index,
            runner_registry,
        )
    }

    pub fn build_with_dynamic_components(
        self,
        dynamic_components: HashMap<NodePath, (AbsoluteComponentUrl, Option<String>)>,
        decls_by_url: HashMap<Url, (ComponentDecl, Option<ConfigFields>)>,
        runtime_config: Arc<RuntimeConfig>,
        component_id_index: Arc<ComponentIdIndex>,
        runner_registry: RunnerRegistry,
    ) -> BuildModelResult {
        let mut result = BuildModelResult::new();

        let (dynamic_components, mut dynamic_component_errors) =
            Self::load_dynamic_components(dynamic_components);
        result.errors.append(&mut dynamic_component_errors);

        // Initialize the model with an empty `instances` map.
        let mut model = ComponentModelForAnalyzer {
            top_instance: TopInstanceForAnalyzer::new(
                runtime_config.namespace_capabilities.clone(),
                runtime_config.builtin_capabilities.clone(),
            ),
            instances: HashMap::new(),
            policy_checker: GlobalPolicyChecker::new(Arc::clone(&runtime_config)),
            component_id_index,
        };

        let root_url = match &runtime_config.root_component_url {
            None => Some(self.default_root_url.clone()),
            Some(url) => match Url::parse(url.as_str()) {
                Ok(url) => Some(url),
                Err(err) => {
                    result.errors.push(anyhow!(
                        r#"Failed to parse root cm URL "{}" as generic URL: {:?}"#,
                        url.as_str(),
                        err
                    ));
                    None
                }
            },
        };

        // If `root_url` matches a `ComponentDecl` in `decls_by_url`, construct the root
        // instance and then recursively add child instances to the model.
        if let Some(root_url) = root_url {
            match Self::get_decl_by_url(&decls_by_url, &root_url) {
                Err(err) => {
                    result
                        .errors
                        .push(err.context("Failed to parse root URL as fuchsia package URL"));
                }
                Ok(None) => {
                    result
                        .errors
                        .push(anyhow!("Failed to locate root component with URL: {}", &root_url));
                }
                Ok(Some((root_decl, root_config))) => {
                    let root_instance = ComponentInstanceForAnalyzer::new_root(
                        root_decl.clone(),
                        root_config.clone(),
                        root_url.to_string(),
                        Arc::clone(&model.top_instance),
                        Arc::clone(&runtime_config),
                        model.policy_checker.clone(),
                        Arc::clone(&model.component_id_index),
                        runner_registry,
                        false,
                    );

                    Self::add_descendants(
                        &root_instance,
                        &decls_by_url,
                        &dynamic_components,
                        &mut model,
                        &mut result,
                    );

                    model
                        .instances
                        .insert(NodePath::from(root_instance.abs_moniker().clone()), root_instance);

                    result.model = Some(Arc::new(model));
                }
            }
        }

        result
    }

    // Adds all descendants of `instance` to `model`, also inserting each new instance
    // in the `children` map of its parent, including children denoted in
    // `dynamic_components`.
    fn add_descendants(
        instance: &Arc<ComponentInstanceForAnalyzer>,
        decls_by_url: &HashMap<Url, (ComponentDecl, Option<ConfigFields>)>,
        dynamic_components: &HashMap<AbsoluteMoniker, Vec<Child>>,
        model: &mut ComponentModelForAnalyzer,
        result: &mut BuildModelResult,
    ) {
        let mut children = vec![];
        for child_decl in instance.decl.children.iter() {
            let child_moniker = match ChildMoniker::try_new(&child_decl.name, None) {
                Ok(cm) => cm,
                Err(err) => {
                    result.errors.push(anyhow!(err));
                    continue;
                }
            };
            match Self::get_absolute_child_url(&child_decl.url, instance) {
                Ok(url) => {
                    children.push(Child {
                        child_moniker,
                        url,
                        environment: child_decl.environment.clone(),
                    });
                }
                Err(err) => {
                    result.errors.push(anyhow!(err));
                }
            }
        }
        if let Some(dynamic_children) = dynamic_components.get(instance.abs_moniker()) {
            children.append(
                &mut dynamic_children
                    .into_iter()
                    .map(|dynamic_child| dynamic_child.clone())
                    .collect(),
            );
        }

        for child in children.iter() {
            match Self::get_absolute_child_url(&child.url.to_string(), instance) {
                Ok(url) => {
                    let absolute_url = url;
                    if child.child_moniker.name().is_empty() {
                        result.errors.push(anyhow!(BuildAnalyzerModelError::InvalidChildDecl(
                            absolute_url.to_string(),
                            NodePath::from(instance.abs_moniker().clone()).to_string(),
                        )));
                        continue;
                    }

                    match Self::get_decl_by_url(decls_by_url, &absolute_url)
                        .context("Failed to parse absolute child URL")
                    {
                        Err(err) => {
                            result.errors.push(err);
                        }
                        Ok(Some((child_component_decl, child_config))) => {
                            match ComponentInstanceForAnalyzer::new_for_child(
                                child,
                                absolute_url.to_string(),
                                child_component_decl.clone(),
                                child_config.clone(),
                                Arc::clone(instance),
                                model.policy_checker.clone(),
                                Arc::clone(&model.component_id_index),
                            ) {
                                Ok(child_instance) => {
                                    Self::add_descendants(
                                        &child_instance,
                                        decls_by_url,
                                        dynamic_components,
                                        model,
                                        result,
                                    );

                                    instance.add_child(
                                        child.child_moniker.clone(),
                                        Arc::clone(&child_instance),
                                    );

                                    model.instances.insert(
                                        NodePath::from(child_instance.abs_moniker().clone()),
                                        child_instance,
                                    );
                                }
                                Err(err) => {
                                    result.errors.push(anyhow!(err));
                                }
                            }
                        }
                        Ok(None) => {
                            result.errors.push(anyhow!(
                                BuildAnalyzerModelError::ComponentDeclNotFound(
                                    absolute_url.to_string(),
                                    NodePath::from(instance.abs_moniker().clone()).to_string(),
                                )
                            ));
                        }
                    }
                }
                Err(err) => {
                    result.errors.push(anyhow!(err));
                }
            }
        }
    }

    // Given a component instance and the url `child_url` of a child of that instance,
    // returns an absolute url for the child.
    fn get_absolute_child_url(
        child_url: &str,
        instance: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<Url, BuildAnalyzerModelError> {
        let err = BuildAnalyzerModelError::MalformedUrl(
            instance.url().to_string(),
            instance.node_path().to_string(),
        );

        match Url::parse(child_url) {
            Ok(url) => Ok(url),
            Err(url::ParseError::RelativeUrlWithoutBase) => {
                let absolute_prefix = match component_has_relative_url(instance) {
                    true => find_first_absolute_ancestor_url(instance).map_err(|_| err),
                    false => Url::parse(instance.url()).map_err(|_| err),
                }?;
                Ok(absolute_prefix
                    .join(child_url)
                    .expect("failed to join child URL to absolute prefix"))
            }
            _ => Err(err),
        }
    }

    fn get_decl_by_url<'a>(
        decls_by_url: &'a HashMap<Url, (ComponentDecl, Option<ConfigFields>)>,
        url: &Url,
    ) -> Result<Option<&'a (ComponentDecl, Option<ConfigFields>)>> {
        // Non-`fuchsia-pkg` URLs are not matched with nuance: they must precisely match an entry in
        // `decls_by_url`.
        if url.scheme() != "fuchsia-pkg" {
            return Ok(decls_by_url.get(url));
        }

        let fuchsia_component_url = AbsoluteComponentUrl::parse(url.as_str())
            .context("Failed to parse component fuchsia-pkg URL as absolute package URL")?;

        // Gather both strong and weak URL matches against `fuchsia_component_url`.
        let decl_url_matches = decls_by_url
            .keys()
            .filter_map(|decl_url| {
                if decl_url.scheme() != "fuchsia-pkg" {
                    None
                } else if let Ok(decl_fuchsia_pkg_url) =
                    AbsoluteComponentUrl::parse(decl_url.as_str())
                {
                    match match_absolute_component_urls(
                        &decl_fuchsia_pkg_url,
                        &fuchsia_component_url,
                    ) {
                        PkgUrlMatch::NoMatch => None,
                        pkg_url_match => Some((decl_url, pkg_url_match)),
                    }
                } else {
                    None
                }
            })
            .collect::<Vec<(&Url, PkgUrlMatch)>>();

        // Return best match. Emit warning or error when multiple matches are found.
        if decl_url_matches.len() == 0 {
            return Ok(None);
        } else if decl_url_matches.len() == 1 {
            if decl_url_matches[0].1 == PkgUrlMatch::WeakMatch {
                tracing::warn!(
                    "Weak component URL match: {} matches {}",
                    url,
                    decl_url_matches[0].0
                );
            }
            return Ok(decls_by_url.get(decl_url_matches[0].0));
        } else {
            let strong_decl_url_matches = decl_url_matches
                .iter()
                .filter_map(|(url, url_match)| match url_match {
                    PkgUrlMatch::StrongMatch => Some(*url),
                    _ => None,
                })
                .collect::<Vec<&Url>>();

            if strong_decl_url_matches.len() == 0 {
                tracing::warn!(
                    "Multiple weak component URL matches for {}; matching to first: {}",
                    url,
                    decl_url_matches[0].0
                );
                return Ok(decls_by_url.get(decl_url_matches[0].0));
            } else {
                if strong_decl_url_matches.len() > 1 {
                    tracing::error!(
                        "Multiple strong package URL matches for {}; matching to first: {}",
                        url,
                        strong_decl_url_matches[0]
                    );
                }
                return Ok(decls_by_url.get(strong_decl_url_matches[0]));
            }
        }
    }
}

/// `ComponentModelForAnalyzer` owns a representation of the v2 component graph and
/// supports lookup of component instances by `NodePath`.
#[derive(Default)]
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

    pub fn get_root_instance(
        self: &Arc<Self>,
    ) -> Result<Arc<ComponentInstanceForAnalyzer>, ComponentInstanceError> {
        self.get_instance(&NodePath::absolute_from_vec(vec![]))
    }

    /// Returns the component instance corresponding to `id` if it is present in the model, or an
    /// `InstanceNotFound` error if not.
    pub fn get_instance(
        self: &Arc<Self>,
        id: &NodePath,
    ) -> Result<Arc<ComponentInstanceForAnalyzer>, ComponentInstanceError> {
        match self.instances.get(id) {
            Some(instance) => Ok(Arc::clone(instance)),
            None => Err(ComponentInstanceError::instance_not_found(
                AbsoluteMoniker::parse_str(&id.to_string()).unwrap(),
            )),
        }
    }

    /// Checks the routing for all capabilities of the specified types that are `used` by `target`.
    pub fn check_routes_for_instance(
        self: &Arc<Self>,
        target: &Arc<ComponentInstanceForAnalyzer>,
        capability_types: &HashSet<CapabilityTypeName>,
    ) -> HashMap<CapabilityTypeName, Vec<VerifyRouteResult>> {
        let mut results = HashMap::new();
        for capability_type in capability_types.iter() {
            results.insert(capability_type.clone(), vec![]);
        }

        for use_decl in target.decl.uses.iter().filter(|&u| capability_types.contains(&u.into())) {
            let type_results = results
                .get_mut(&CapabilityTypeName::from(use_decl))
                .expect("expected results for capability type");
            for result in self.check_use_capability(use_decl, &target) {
                type_results.push(result);
            }
        }

        for expose_decl in
            target.decl.exposes.iter().filter(|&e| capability_types.contains(&e.into()))
        {
            let type_results = results
                .get_mut(&CapabilityTypeName::from(expose_decl))
                .expect("expected results for capability type");
            if let Some(result) = self.check_use_exposed_capability(expose_decl, &target) {
                type_results.push(result);
            }
        }

        if capability_types.contains(&CapabilityTypeName::Runner) {
            if let Some(ref program) = target.decl.program {
                let type_results = results
                    .get_mut(&CapabilityTypeName::Runner)
                    .expect("expected results for capability type");
                if let Some(result) = self.check_program_runner(program, &target) {
                    type_results.push(result);
                }
            }
        }

        if capability_types.contains(&CapabilityTypeName::Resolver) {
            let type_results = results
                .get_mut(&CapabilityTypeName::Resolver)
                .expect("expected results for capability type");
            type_results.push(self.check_resolver(&target));
        }

        results
    }

    /// Given a `UseDecl` for a capability at an instance `target`, first routes the capability
    /// to its source and then validates the source.
    pub fn check_use_capability(
        self: &Arc<Self>,
        use_decl: &UseDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Vec<VerifyRouteResult> {
        let mut results = Vec::new();
        let route_result = match use_decl.clone() {
            UseDecl::Directory(use_directory_decl) => {
                let capability = use_directory_decl.source_name.clone();
                match Self::route_capability_sync(
                    RouteRequest::UseDirectory(use_directory_decl),
                    target,
                ) {
                    Ok((source, route)) => (Ok((source, vec![route])), capability),
                    // Ignore any route that failed due to a void offer to a target with an
                    // optional dependency on the capability.
                    Err(RoutingError::AvailabilityRoutingError(
                        AvailabilityRoutingError::OfferFromVoidToOptionalTarget,
                    )) => return vec![],
                    Err(err) => (Err(err.into()), capability),
                }
            }
            UseDecl::Event(use_event_decl) => {
                let capability = use_event_decl.target_name.clone();
                match self.uses_event_source_protocol(&target.decl) {
                    true => {
                        match Self::route_capability_sync(
                            RouteRequest::UseEvent(use_event_decl),
                            target,
                        ) {
                            Ok((source, route)) => (Ok((source, vec![route])), capability),
                            // Ignore any route that failed due to a void offer to a target with an
                            // optional dependency on the capability.
                            Err(RoutingError::AvailabilityRoutingError(
                                AvailabilityRoutingError::OfferFromVoidToOptionalTarget,
                            )) => return vec![],
                            Err(err) => (Err(err.into()), capability),
                        }
                    }
                    false => (
                        Err(AnalyzerModelError::MissingEventSourceProtocol(capability.to_string())),
                        capability,
                    ),
                }
            }
            UseDecl::Protocol(use_protocol_decl) => {
                let capability = use_protocol_decl.source_name.clone();
                match Self::route_capability_sync(
                    RouteRequest::UseProtocol(use_protocol_decl),
                    target,
                ) {
                    Ok((source, route)) => (Ok((source, vec![route])), capability),
                    // Ignore any route that failed due to a void offer to a target with an
                    // optional dependency on the capability.
                    Err(RoutingError::AvailabilityRoutingError(
                        AvailabilityRoutingError::OfferFromVoidToOptionalTarget,
                    )) => return vec![],
                    Err(err) => (Err(err.into()), capability),
                }
            }
            UseDecl::Service(use_service_decl) => {
                let capability = use_service_decl.source_name.clone();
                match Self::route_capability_sync(
                    RouteRequest::UseService(use_service_decl.clone()),
                    target,
                ) {
                    Ok((source, route)) => (Ok((source, vec![route])), capability),
                    // Ignore any route that failed due to a void offer to a target with an
                    // optional dependency on the capability.
                    Err(RoutingError::AvailabilityRoutingError(
                        AvailabilityRoutingError::OfferFromVoidToOptionalTarget,
                    )) => return vec![],
                    Err(err) => (Err(err.into()), capability),
                }
            }
            UseDecl::Storage(use_storage_decl) => {
                let capability = use_storage_decl.source_name.clone();
                match Self::route_storage_and_backing_directory_sync(use_storage_decl, target) {
                    Ok((storage_source, _relative_moniker, storage_route, dir_route)) => (
                        Ok((
                            RouteSource::StorageBackingDirectory(storage_source),
                            vec![storage_route, dir_route],
                        )),
                        capability,
                    ),
                    // Ignore any route that failed due to a void offer to a target with an
                    // optional dependency on the capability.
                    Err(RoutingError::AvailabilityRoutingError(
                        AvailabilityRoutingError::OfferFromVoidToOptionalTarget,
                    )) => return vec![],
                    Err(err) => (Err(err.into()), capability),
                }
            }
            UseDecl::EventStream(use_event_stream_decl) => {
                let capability = use_event_stream_decl.source_name.clone();
                match Self::route_capability_sync(
                    RouteRequest::UseEventStream(use_event_stream_decl),
                    target,
                ) {
                    Ok((source, route)) => (Ok((source, vec![route])), capability),
                    Err(err) => (Err(err.into()), capability),
                }
            }
            _ => unimplemented![],
        };
        match route_result {
            (Ok((source, routes)), capability) => match self.check_use_source(&source) {
                Ok(()) => {
                    for route in routes.into_iter() {
                        results.push(VerifyRouteResult {
                            using_node: target.node_path(),
                            capability: capability.clone(),
                            result: Ok(route.into()),
                        });
                    }
                }
                Err(err) => {
                    results.push(VerifyRouteResult {
                        using_node: target.node_path(),
                        capability: capability.clone(),
                        result: Err(err.into()),
                    });
                }
            },
            (Err(err), capability) => results.push(VerifyRouteResult {
                using_node: target.node_path(),
                capability: capability.clone(),
                result: Err(err.into()),
            }),
        }
        results
    }

    /// Given a `ExposeDecl` for a capability at an instance `target`, checks whether the capability
    /// can be used from an expose declaration. If so, routes the capability to its source and then
    /// validates the source.
    pub fn check_use_exposed_capability(
        self: &Arc<Self>,
        expose_decl: &ExposeDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Option<VerifyRouteResult> {
        match self.request_from_expose(expose_decl) {
            Some(request) => {
                let result = match Self::route_capability_sync(request, target) {
                    Ok((source, route)) => match self.check_use_source(&source) {
                        Ok(()) => Ok(route.into()),
                        Err(err) => Err(err.into()),
                    },
                    Err(err) => Err(AnalyzerModelError::from(err).into()),
                };
                Some(VerifyRouteResult {
                    using_node: target.node_path(),
                    capability: expose_decl.target_name().clone(),
                    result,
                })
            }
            None => None,
        }
    }

    /// Given a `ProgramDecl` for a component instance, checks whether the specified runner has
    /// a valid capability route.
    pub fn check_program_runner(
        self: &Arc<Self>,
        program_decl: &ProgramDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Option<VerifyRouteResult> {
        match program_decl.runner {
            Some(ref runner) => {
                let mut route = RouteMap::from_segments(vec![RouteSegment::RequireRunner {
                    node_path: target.node_path(),
                    runner: runner.clone(),
                }]);
                match Self::route_capability_sync(RouteRequest::Runner(runner.clone()), target) {
                    Ok((_source, mut segments)) => {
                        route.append(&mut segments);
                        Some(VerifyRouteResult {
                            using_node: target.node_path(),
                            capability: runner.clone(),
                            result: Ok(route.into()),
                        })
                    }
                    Err(err) => Some(VerifyRouteResult {
                        using_node: target.node_path(),
                        capability: runner.clone(),
                        result: Err(AnalyzerModelError::from(err).into()),
                    }),
                }
            }
            None => None,
        }
    }

    /// Given a component instance, extracts the URL scheme for that instance and looks for a
    /// resolver for that scheme in the instance's environment, recording an error if none
    /// is found. If a resolver is found, checks that it has a valid capability route.
    pub fn check_resolver(
        self: &Arc<Self>,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> VerifyRouteResult {
        let url = Url::parse(target.url()).expect("failed to parse target URL");
        let scheme = url.scheme();
        let mut route = vec![RouteSegment::RequireResolver {
            node_path: target.node_path(),
            scheme: scheme.to_string(),
        }];

        let check_route = match target.environment.get_registered_resolver(scheme) {
            Ok(Some((ExtendedInstanceInterface::Component(instance), resolver))) => {
                match Self::route_capability_sync(
                    RouteRequest::Resolver(resolver.clone()),
                    &instance,
                ) {
                    Ok((_source, route)) => VerifyRouteResult {
                        using_node: target.node_path(),
                        capability: resolver.resolver,
                        result: Ok(route.into()),
                    },
                    Err(err) => VerifyRouteResult {
                        using_node: target.node_path(),
                        capability: resolver.resolver,
                        result: Err(AnalyzerModelError::from(err).into()),
                    },
                }
            }
            Ok(Some((ExtendedInstanceInterface::AboveRoot(_), resolver))) => {
                match self.get_builtin_resolver_decl(&resolver) {
                    Ok(decl) => {
                        let mut route = RouteMap::new();
                        route.push(RouteSegment::ProvideAsBuiltin { capability: decl });
                        VerifyRouteResult {
                            using_node: target.node_path(),
                            capability: resolver.resolver,
                            result: Ok(route.into()),
                        }
                    }
                    Err(err) => VerifyRouteResult {
                        using_node: target.node_path(),
                        capability: resolver.resolver,
                        result: Err(err.into()),
                    },
                }
            }
            Ok(None) => VerifyRouteResult {
                using_node: target.node_path(),
                capability: "".into(),
                result: Err(AnalyzerModelError::MissingResolverForScheme(scheme.to_string()).into()),
            },
            Err(err) => VerifyRouteResult {
                using_node: target.node_path(),
                capability: "".into(),
                result: Err(AnalyzerModelError::from(err).into()),
            },
        };

        let check_result = match check_route.result {
            Ok(mut segments) => {
                route.append(&mut segments);
                Ok(route.into())
            }
            Err(err) => Err(err.into()),
        };
        VerifyRouteResult {
            using_node: target.node_path(),
            capability: check_route.capability,
            result: check_result,
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

    // Checks properties of a capability source that are necessary to use the capability
    // and that are possible to verify statically.
    fn check_use_source(
        &self,
        route_source: &RouteSource<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match route_source {
            RouteSource::Directory(source, _) => self.check_directory_source(source),
            RouteSource::Event(_) => Ok(()),
            RouteSource::EventStream(source) => self.check_protocol_source(source),
            RouteSource::Protocol(source) => self.check_protocol_source(source),
            RouteSource::Service(source) => self.check_service_source(source),
            RouteSource::StorageBackingDirectory(source) => self.check_storage_source(source),
            _ => unimplemented![],
        }
    }

    // If the source of a directory capability is a component instance, checks that that
    // instance is executable.
    fn check_directory_source(
        &self,
        source: &CapabilitySourceInterface<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match source {
            CapabilitySourceInterface::Component { component: weak, .. } => {
                self.check_executable(&weak.upgrade()?)
            }
            CapabilitySourceInterface::Namespace { .. } => Ok(()),
            CapabilitySourceInterface::Builtin { .. } => Ok(()),
            CapabilitySourceInterface::Framework { .. } => Ok(()),
            _ => unimplemented![],
        }
    }

    // If the source of a protocol capability is a component instance, checks that that
    // instance is executable.
    //
    // If the source is a capability, checks that the protocol is the `StorageAdmin`
    // protocol and that the source is a valid storage capability.
    fn check_protocol_source(
        &self,
        source: &CapabilitySourceInterface<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match source {
            CapabilitySourceInterface::Component { component: weak, .. } => {
                self.check_executable(&weak.upgrade()?)
            }
            CapabilitySourceInterface::Namespace { .. } => Ok(()),
            CapabilitySourceInterface::Capability { source_capability, component: weak } => {
                self.check_protocol_capability_source(&weak.upgrade()?, &source_capability)
            }
            CapabilitySourceInterface::Builtin { .. } => Ok(()),
            CapabilitySourceInterface::Framework { .. } => Ok(()),
            _ => unimplemented![],
        }
    }

    // A helper function validating a source of type `Capability` for a protocol capability.
    // If the protocol is the `StorageAdmin` protocol, then it should have a valid storage
    // source.
    fn check_protocol_capability_source(
        &self,
        source_component: &Arc<ComponentInstanceForAnalyzer>,
        source_capability: &ComponentCapability,
    ) -> Result<(), AnalyzerModelError> {
        let source_capability_name = source_capability
            .source_capability_name()
            .expect("failed to get source capability name");

        match source_capability.source_name().map(|name| name.to_string()).as_deref() {
            Some(fsys::StorageAdminMarker::PROTOCOL_NAME) => {
                match source_component.decl.find_storage_source(source_capability_name) {
                    Some(_) => Ok(()),
                    None => Err(AnalyzerModelError::InvalidSourceCapability(
                        source_capability_name.to_string(),
                        fsys::StorageAdminMarker::PROTOCOL_NAME.to_string(),
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

    // If the source of a service capability is a component instance, checks that that
    // instance is executable.
    fn check_service_source(
        &self,
        source: &CapabilitySourceInterface<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        match source {
            CapabilitySourceInterface::Component { component: weak, .. } => {
                self.check_executable(&weak.upgrade()?)
            }
            CapabilitySourceInterface::Namespace { .. } => Ok(()),
            _ => unimplemented![],
        }
    }

    // If the source of a storage backing directory is a component instance, checks that that
    // instance is executable.
    fn check_storage_source(
        &self,
        source: &StorageCapabilitySource<ComponentInstanceForAnalyzer>,
    ) -> Result<(), AnalyzerModelError> {
        if let Some(provider) = &source.storage_provider {
            self.check_executable(provider)?
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
    fn check_executable(
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

    // Routes a capability from a `ComponentInstanceForAnalyzer` and panics if the future returned by
    // `route_capability` is not ready immediately.
    //
    // TODO(fxbug.dev/87204): Remove this function and use `route_capability` directly when Scrutiny's
    // `DataController`s allow async function calls.
    pub fn route_capability_sync(
        request: RouteRequest,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<
        (
            RouteSource<ComponentInstanceForAnalyzer>,
            <<ComponentInstanceForAnalyzer as ComponentInstanceInterface>::DebugRouteMapper as DebugRouteMapper>::RouteMap,
        ),
        RoutingError>
    {
        route_capability(request, target).now_or_never().expect("future was not ready immediately")
    }

    pub fn route_event_stream_sync(
        request: UseEventStreamDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
        map: &mut Vec<Arc<ComponentInstanceForAnalyzer>>,
    ) -> Result<
        (
            RouteSource<ComponentInstanceForAnalyzer>,
            <<ComponentInstanceForAnalyzer as ComponentInstanceInterface>::DebugRouteMapper as DebugRouteMapper>::RouteMap,
        ),
        RoutingError>
    {
        route_event_stream_capability(request, target, map)
            .now_or_never()
            .expect("future was not ready immediately")
    }

    // Routes a storage capability and its backing directory from a `ComponentInstanceForAnalyzer` and
    // panics if the future returned by `route_storage_and_backing_directory` is not ready immediately.
    //
    // TODO(fxbug.dev/87204): Remove this function and use `route_capability` directly when Scrutiny's
    // `DataController`s allow async function calls.
    fn route_storage_and_backing_directory_sync(
        use_decl: UseStorageDecl,
        target: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<
            (
                StorageCapabilitySource<ComponentInstanceForAnalyzer>,
                InstancedRelativeMoniker,
                <<ComponentInstanceForAnalyzer as ComponentInstanceInterface>::DebugRouteMapper as DebugRouteMapper>::RouteMap,
                <<ComponentInstanceForAnalyzer as ComponentInstanceInterface>::DebugRouteMapper as DebugRouteMapper>::RouteMap,
            ),
        RoutingError>
    {
        route_storage_and_backing_directory(use_decl, target)
            .now_or_never()
            .expect("future was not ready immediately")
    }

    pub fn collect_config_by_url(&self) -> anyhow::Result<BTreeMap<String, ConfigFields>> {
        let mut configs = BTreeMap::new();
        for instance in self.instances.values() {
            if let Some(fields) = instance.config_fields() {
                configs.insert(instance.url().to_string(), fields.clone());
            }
        }
        Ok(configs)
    }
}

#[derive(Clone)]
pub struct Child {
    pub child_moniker: ChildMoniker,
    pub url: Url,
    pub environment: Option<String>,
}

#[cfg(test)]
mod tests {
    use {
        super::ModelBuilderForAnalyzer,
        crate::{environment::BOOT_SCHEME, node_path::NodePath, ComponentModelForAnalyzer},
        anyhow::Result,
        cm_moniker::InstancedAbsoluteMoniker,
        cm_rust::{
            Availability, CapabilityName, CapabilityPath, ComponentDecl, DependencyType,
            RegistrationSource, ResolverRegistration, RunnerRegistration, UseProtocolDecl,
            UseSource, UseStorageDecl,
        },
        cm_rust_testing::{ChildDeclBuilder, ComponentDeclBuilder, EnvironmentDeclBuilder},
        config_encoder::ConfigFields,
        fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_component_internal as component_internal,
        maplit::hashmap,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker},
        routing::{
            component_id_index::ComponentIdIndex,
            component_instance::{
                ComponentInstanceInterface, ExtendedInstanceInterface,
                WeakExtendedInstanceInterface,
            },
            config::RuntimeConfig,
            environment::{EnvironmentInterface, RunnerRegistry},
            error::ComponentInstanceError,
            RouteRequest,
        },
        std::{
            collections::HashMap,
            convert::{TryFrom, TryInto},
            iter::FromIterator,
            sync::Arc,
        },
        url::Url,
    };

    const TEST_URL_PREFIX: &str = "test:///";

    fn make_test_url(component_name: &str) -> Url {
        Url::parse(&format!("{}{}", TEST_URL_PREFIX, component_name)).unwrap()
    }

    fn make_decl_map(
        components: Vec<(&'static str, ComponentDecl)>,
    ) -> HashMap<Url, (ComponentDecl, Option<ConfigFields>)> {
        HashMap::from_iter(
            components.into_iter().map(|(name, decl)| (make_test_url(name), (decl, None))),
        )
    }

    // Builds a model with structure `root -- child`, retrieves each of the 2 resulting component
    // instances, and tests their public methods.
    #[fuchsia::test]
    fn build_model() -> Result<()> {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("child").build()),
            ("child", ComponentDeclBuilder::new().build()),
        ];

        let config = Arc::new(RuntimeConfig::default());
        let url = make_test_url("root");
        let build_model_result = ModelBuilderForAnalyzer::new(url).build(
            make_decl_map(components),
            config,
            Arc::new(ComponentIdIndex::default()),
            RunnerRegistry::default(),
        );
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 2);

        let root_instance =
            model.get_instance(&NodePath::absolute_from_vec(vec![])).expect("root instance");
        let child_instance = model
            .get_instance(&NodePath::absolute_from_vec(vec!["child"]))
            .expect("child instance");

        let other_id = NodePath::absolute_from_vec(vec!["other"]);
        let get_other_result = model.get_instance(&other_id);
        assert_eq!(
            get_other_result.err().unwrap().to_string(),
            ComponentInstanceError::instance_not_found(
                AbsoluteMoniker::parse_str(&other_id.to_string()).unwrap()
            )
            .to_string()
        );

        // Include tests for `.instanced_moniker()` alongside `.abs_moniker()`
        // until`.instanced_moniker()` is removed from the public API.
        assert_eq!(root_instance.abs_moniker(), &AbsoluteMoniker::root());
        assert_eq!(root_instance.instanced_moniker(), &InstancedAbsoluteMoniker::root());

        assert_eq!(child_instance.abs_moniker(), &AbsoluteMoniker::parse_str("/child").unwrap());
        assert_eq!(
            child_instance.instanced_moniker(),
            &InstancedAbsoluteMoniker::parse_str("/child:0").unwrap()
        );

        match root_instance.try_get_parent()? {
            ExtendedInstanceInterface::AboveRoot(_) => {}
            _ => panic!("root instance's parent should be `AboveRoot`"),
        }
        match child_instance.try_get_parent()? {
            ExtendedInstanceInterface::Component(component) => {
                assert_eq!(component.abs_moniker(), root_instance.abs_moniker());
                assert_eq!(component.instanced_moniker(), root_instance.instanced_moniker())
            }
            _ => panic!("child instance's parent should be root component"),
        }

        let get_child = root_instance
            .resolve()
            .map(|locked| locked.get_child(&ChildMoniker::try_new("child", None).unwrap()))?;
        assert!(get_child.is_some());
        assert_eq!(get_child.as_ref().unwrap().abs_moniker(), child_instance.abs_moniker());
        assert_eq!(get_child.unwrap().instanced_moniker(), child_instance.instanced_moniker());

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
                assert_eq!(component.upgrade()?.abs_moniker(), root_instance.abs_moniker());
                assert_eq!(
                    component.upgrade()?.instanced_moniker(),
                    root_instance.instanced_moniker()
                )
            }
            _ => panic!("child environment's parent should be root component"),
        }

        root_instance.try_get_policy_checker()?;
        root_instance.try_get_component_id_index()?;

        child_instance.try_get_policy_checker()?;
        child_instance.try_get_component_id_index()?;

        assert!(root_instance.resolve().is_ok());
        assert!(child_instance.resolve().is_ok());

        Ok(())
    }

    // Builds a model with structure `root -- child` where the child's URL is expressed in
    // the root manifest as a relative URL.
    #[fuchsia::test]
    fn build_model_with_relative_url() {
        let root_decl = ComponentDeclBuilder::new()
            .add_child(ChildDeclBuilder::new().name("child").url("#child").build())
            .build();
        let child_decl = ComponentDeclBuilder::new().build();
        let root_url = make_test_url("root");
        let absolute_child_url = Url::parse(&format!("{}#child", root_url)).unwrap();

        let mut decls_by_url = HashMap::new();
        decls_by_url.insert(root_url.clone(), (root_decl, None));
        decls_by_url.insert(absolute_child_url.clone(), (child_decl, None));

        let config = Arc::new(RuntimeConfig::default());
        let build_model_result = ModelBuilderForAnalyzer::new(root_url).build(
            decls_by_url,
            config,
            Arc::new(ComponentIdIndex::default()),
            RunnerRegistry::default(),
        );
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 2);

        let child_instance = model
            .get_instance(&NodePath::absolute_from_vec(vec!["child"]))
            .expect("child instance");

        assert_eq!(child_instance.url(), absolute_child_url.as_str());
    }

    // Spot-checks that `route_capability` returns immediately when routing a capability from a
    // `ComponentInstanceForAnalyzer`. In addition, updates to that method should
    // be reviewed to make sure that this property holds; otherwise, `ComponentModelForAnalyzer`'s
    // sync methods may panic.
    #[fuchsia::test]
    fn route_capability_is_sync() {
        let components = vec![("root", ComponentDeclBuilder::new().build())];

        let config = Arc::new(RuntimeConfig::default());
        let url = make_test_url("root");
        let build_model_result = ModelBuilderForAnalyzer::new(url).build(
            make_decl_map(components),
            config,
            Arc::new(ComponentIdIndex::default()),
            RunnerRegistry::default(),
        );
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 1);

        let root_instance =
            model.get_instance(&NodePath::absolute_from_vec(vec![])).expect("root instance");

        // Panics if the future returned by `route_capability` was not ready immediately.
        // If no panic, discard the result.
        let _ = ComponentModelForAnalyzer::route_capability_sync(
            RouteRequest::UseProtocol(UseProtocolDecl {
                source: UseSource::Parent,
                source_name: "bar_svc".into(),
                target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            }),
            &root_instance,
        );
    }

    // Checks that `route_capability` returns immediately when routing a capability from a
    // `ComponentInstanceForAnalyzer`. In addition, updates to that method should
    // be reviewed to make sure that this property holds; otherwise, `ComponentModelForAnalyzer`'s
    // sync methods may panic.
    #[fuchsia::test]
    fn route_storage_and_backing_directory_is_sync() {
        let components = vec![("root", ComponentDeclBuilder::new().build())];

        let config = Arc::new(RuntimeConfig::default());
        let cm_url = cm_types::Url::new(make_test_url("root").to_string())
            .expect("failed to parse root component url");
        let url = Url::parse(cm_url.as_str()).expect("failed to parse root component url");
        let build_model_result = ModelBuilderForAnalyzer::new(url).build(
            make_decl_map(components),
            config,
            Arc::new(ComponentIdIndex::default()),
            RunnerRegistry::default(),
        );
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 1);

        let root_instance =
            model.get_instance(&NodePath::absolute_from_vec(vec![])).expect("root instance");

        // Panics if the future returned by `route_storage_and_backing_directory` was not ready immediately.
        // If no panic, discard the result.
        let _ = ComponentModelForAnalyzer::route_storage_and_backing_directory_sync(
            UseStorageDecl {
                source_name: "cache".into(),
                target_path: "/storage".try_into().unwrap(),
                availability: Availability::Required,
            },
            &root_instance,
        );
    }

    // Builds a model with structure `root -- child` in which the child environment extends the root's.
    // Checks that the child has access to the inherited runner and resolver registrations through its
    // environment.
    #[fuchsia::test]
    fn environment_inherits() -> Result<()> {
        let child_env_name = "child_env";
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

        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_child(
                        ChildDeclBuilder::new_lazy_child("child").environment(child_env_name),
                    )
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name(child_env_name)
                            .extends(fdecl::EnvironmentExtends::Realm)
                            .add_resolver(child_resolver_registration.clone())
                            .add_runner(child_runner_registration.clone())
                            .build(),
                    )
                    .build(),
            ),
            ("child", ComponentDeclBuilder::new().build()),
        ];

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

        let cm_url = cm_types::Url::new(make_test_url("root").to_string())
            .expect("failed to parse root component url");
        let url = Url::parse(cm_url.as_str()).expect("failed to parse root component url");
        let build_model_result = ModelBuilderForAnalyzer::new(url).build(
            make_decl_map(components),
            Arc::new(config),
            Arc::new(ComponentIdIndex::default()),
            RunnerRegistry::from_decl(&vec![builtin_runner_registration]),
        );
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 2);

        let child_instance = model
            .get_instance(&NodePath::absolute_from_vec(vec!["child"]))
            .expect("child instance");

        let get_child_runner_result = child_instance
            .environment()
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
            .environment()
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

    fn decl(id: &str) -> ComponentDecl {
        // Identify decls by a single child named `id`.
        ComponentDeclBuilder::new().add_child(ChildDeclBuilder::new_lazy_child(id)).build()
    }

    #[fuchsia::test]
    fn get_decl_by_url_none() {
        let beta_beta_urls = vec![
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta#beta.cm").unwrap(),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#beta.cm").unwrap(),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap(),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap(),
        ];
        let decls_by_url_no_beta_beta = hashmap! {
            Url::parse("fuchsia-pkg://test.fuchsia.com/alpha#beta.cm").unwrap() => (decl("alpha_beta"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#alpha.cm").unwrap() => (decl("beta_alpha"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/gamma?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap() => (decl("gamma_beta"), None),
        };

        for beta_beta_url in beta_beta_urls.iter() {
            let result =
                ModelBuilderForAnalyzer::get_decl_by_url(&decls_by_url_no_beta_beta, beta_beta_url);
            assert!(result.is_ok());
            assert_eq!(None, result.ok().unwrap());
        }
    }

    #[fuchsia::test]
    fn get_decl_by_url_fuchsia_boot() {
        let fuchsia_boot_url = Url::parse("fuchsia-boot:///#meta/boot.cm").unwrap();
        let fuchsia_boot_component = decl("boot");
        let decls_by_url_with_fuchsia_boot = hashmap! {
            Url::parse("fuchsia-pkg://test.fuchsia.com/alpha#beta.cm").unwrap() => (decl("alpha_beta"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#alpha.cm").unwrap() => (decl("beta_alpha"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/gamma?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap() => (decl("gamma_beta"), None),
            fuchsia_boot_url.clone() => (fuchsia_boot_component.clone(), None),
        };

        let result = ModelBuilderForAnalyzer::get_decl_by_url(
            &decls_by_url_with_fuchsia_boot,
            &fuchsia_boot_url,
        );

        assert!(result.is_ok());
        assert_eq!(Some(&(fuchsia_boot_component, None)), result.ok().unwrap());
    }

    #[fuchsia::test]
    fn get_decl_by_url_bad_url() {
        let bad_url =
            Url::parse("fuchsia-pkg:///test.fuchsia.com/alpha?hash=notahexvalue#meta/alpha.cm")
                .unwrap();
        let empty_decls_by_url = hashmap! {};

        let result = ModelBuilderForAnalyzer::get_decl_by_url(&empty_decls_by_url, &bad_url);

        assert!(result.is_err());
    }

    #[fuchsia::test]
    fn get_decl_by_url_strong() {
        let beta_beta_url = Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap();
        let beta_beta_decl = decl("beta_beta");
        let decls_by_url_with_beta_beta = hashmap! {
            Url::parse("fuchsia-pkg://test.fuchsia.com/alpha#beta.cm").unwrap() => (decl("alpha_beta"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#alpha.cm").unwrap() => (decl("beta_alpha"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/gamma?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap() => (decl("gamma_beta"), None),
            beta_beta_url.clone() => (beta_beta_decl.clone(), None),
        };

        let result =
            ModelBuilderForAnalyzer::get_decl_by_url(&decls_by_url_with_beta_beta, &beta_beta_url);

        assert!(result.is_ok());
        assert_eq!(Some(&(beta_beta_decl, None)), result.ok().unwrap());
    }

    #[fuchsia::test]
    fn get_decl_by_url_strongest() {
        let beta_beta_strong_url = Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap();
        let beta_beta_strong_decl = decl("beta_beta_strong");
        let beta_beta_weak_url_1 =
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#beta.cm").unwrap();
        let beta_beta_weak_decl_1 = decl("beta_beta_weak_1");
        let beta_beta_weak_url_2 = Url::parse("fuchsia-pkg://test.fuchsia.com/beta?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap();
        let beta_beta_weak_decl_2 = decl("beta_beta_weak_2");
        let beta_beta_weak_url_3 =
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta#beta.cm").unwrap();
        let beta_beta_weak_decl_3 = decl("beta_beta_weak_3");
        let decls_by_url_with_4_beta_betas = hashmap! {
            beta_beta_weak_url_1 => (beta_beta_weak_decl_1, None),
            beta_beta_weak_url_2 => (beta_beta_weak_decl_2, None),
            beta_beta_weak_url_3 => (beta_beta_weak_decl_3, None),
            beta_beta_strong_url.clone() => (beta_beta_strong_decl.clone(), None),
        };

        let result = ModelBuilderForAnalyzer::get_decl_by_url(
            &decls_by_url_with_4_beta_betas,
            &beta_beta_strong_url,
        );

        assert!(result.is_ok());
        assert_eq!(Some(&(beta_beta_strong_decl, None)), result.ok().unwrap());
    }

    #[fuchsia::test]
    fn get_decl_by_url_weak() {
        let beta_beta_strong_url = Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap();
        let beta_beta_weak_url = Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap();
        let beta_beta_decl = decl("beta_beta");
        let decls_by_url_with_strong_beta_beta = hashmap! {
            Url::parse("fuchsia-pkg://test.fuchsia.com/alpha#beta.cm").unwrap() => (decl("alpha_beta"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#alpha.cm").unwrap() => (decl("beta_alpha"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/gamma?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap() => (decl("gamma_beta"), None),
            beta_beta_strong_url.clone() => (beta_beta_decl.clone(), None),
        };

        let result = ModelBuilderForAnalyzer::get_decl_by_url(
            &decls_by_url_with_strong_beta_beta,
            &beta_beta_weak_url,
        );

        assert!(result.is_ok());
        assert_eq!(Some(&(beta_beta_decl, None)), result.ok().unwrap());
    }

    #[fuchsia::test]
    fn get_decl_by_url_weak_any() {
        let beta_beta_url_1 = Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap();
        let beta_beta_decl_1 = decl("beta_beta_strong");
        let beta_beta_url_2 = Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#beta.cm").unwrap();
        let beta_beta_decl_2 = decl("beta_beta_weak_1");
        let beta_beta_url_3 = Url::parse("fuchsia-pkg://test.fuchsia.com/beta?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap();
        let beta_beta_decl_3 = decl("beta_beta_weak_2");
        let beta_beta_weakest_url =
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta#beta.cm").unwrap();
        let decls_by_url_3_weak_matches = hashmap! {
            Url::parse("fuchsia-pkg://test.fuchsia.com/alpha#beta.cm").unwrap() => (decl("alpha_beta"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/beta/0#alpha.cm").unwrap() => (decl("beta_alpha"), None),
            Url::parse("fuchsia-pkg://test.fuchsia.com/gamma?hash=0000000000000000000000000000000000000000000000000000000000000000#beta.cm").unwrap() => (decl("gamma_beta"), None),
            beta_beta_url_1 => (beta_beta_decl_1.clone(), None),
            beta_beta_url_2 => (beta_beta_decl_2.clone(), None),
            beta_beta_url_3 => (beta_beta_decl_3.clone(), None),
        };

        let result = ModelBuilderForAnalyzer::get_decl_by_url(
            &decls_by_url_3_weak_matches,
            &beta_beta_weakest_url,
        );

        assert!(result.is_ok());
        let actual_decl = result.ok().unwrap().unwrap();
        assert!(
            beta_beta_decl_1 == actual_decl.0
                || beta_beta_decl_2 == actual_decl.0
                || beta_beta_decl_3 == actual_decl.0
        );
    }
}
