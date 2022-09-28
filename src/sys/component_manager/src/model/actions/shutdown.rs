// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey, ActionSet},
        component::{ComponentInstance, InstanceState, ResolvedInstanceState},
        error::ModelError,
    },
    async_trait::async_trait,
    cm_rust::{
        CapabilityDecl, CapabilityName, ChildRef, CollectionDecl, DependencyType, EnvironmentDecl,
        ExposeDecl, OfferDecl, OfferDirectoryDecl, OfferProtocolDecl, OfferResolverDecl,
        OfferRunnerDecl, OfferServiceDecl, OfferSource, OfferStorageDecl, OfferTarget,
        RegistrationDeclCommon, RegistrationSource, StorageDirectorySource, UseDecl,
        UseDirectoryDecl, UseEventDecl, UseProtocolDecl, UseServiceDecl, UseSource,
    },
    futures::future::select_all,
    moniker::{ChildMoniker, ChildMonikerBase},
    std::collections::{HashMap, HashSet},
    std::fmt,
    std::sync::Arc,
    tracing::error,
};

/// Shuts down all component instances in this component (stops them and guarantees they will never
/// be started again).
pub struct ShutdownAction {}

impl ShutdownAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for ShutdownAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_shutdown(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Shutdown
    }
}

async fn shutdown_component(target: ShutdownInfo) -> Result<ComponentRef, ModelError> {
    match target.ref_ {
        ComponentRef::Self_ => {
            // TODO: Put `self` in a "shutting down" state so that if it creates
            // new instances after this point, they are created in a shut down
            // state.
            target.component.stop_instance(true, false).await?;
        }
        ComponentRef::Child(_) => {
            ActionSet::register(target.component, ShutdownAction::new()).await?;
        }
    }

    Ok(target.ref_.clone())
}

/// Structure which holds bidirectional capability maps used during the
/// shutdown process.
struct ShutdownJob {
    /// A map from users of capabilities to the components that provide those
    /// capabilities
    target_to_sources: HashMap<ComponentRef, Vec<ComponentRef>>,
    /// A map from providers of capabilities to those components which use the
    /// capabilities
    source_to_targets: HashMap<ComponentRef, ShutdownInfo>,
}

/// ShutdownJob encapsulates the logic and state require to shutdown a component.
impl ShutdownJob {
    /// Creates a new ShutdownJob by examining the Component's declaration and
    /// runtime state to build up the necessary data structures to stop
    /// components in the component in dependency order.
    pub async fn new(
        instance: &Arc<ComponentInstance>,
        state: &ResolvedInstanceState,
    ) -> ShutdownJob {
        // `dependency_map` represents the dependency relationships between the
        // nodes in this realm (the children, and the component itself).
        // `dependency_map` maps server => clients (a.k.a. provider => consumers,
        // or source => targets)
        let dependency_map = process_component_dependencies(state);
        let mut source_to_targets: HashMap<ComponentRef, ShutdownInfo> = HashMap::new();

        for (source, targets) in dependency_map {
            let component = match &source {
                ComponentRef::Self_ => instance.clone(),
                ComponentRef::Child(moniker) => {
                    state.get_child(&moniker).expect("component not found in children").clone()
                }
            };

            source_to_targets.insert(
                source.clone(),
                ShutdownInfo { ref_: source, dependents: targets, component },
            );
        }
        // `target_to_sources` is the inverse of `source_to_targets`, and maps a target to all of
        // its dependencies. This inverse mapping gives us a way to do quick lookups when updating
        // `source_to_targets` as we shutdown components in execute().
        let mut target_to_sources: HashMap<ComponentRef, Vec<ComponentRef>> = HashMap::new();
        for provider in source_to_targets.values() {
            // All listed siblings are ones that depend on this child
            // and all those siblings must stop before this one
            for consumer in &provider.dependents {
                // Make or update a map entry for the consumer that points to the
                // list of siblings that offer it capabilities
                target_to_sources
                    .entry(consumer.clone())
                    .or_insert(vec![])
                    .push(provider.ref_.clone());
            }
        }
        let new_job = ShutdownJob { source_to_targets, target_to_sources };
        return new_job;
    }

    /// Perform shutdown of the Component that was used to create this ShutdownJob A Component must
    /// wait to shut down until all its children are shut down.  The shutdown procedure looks at
    /// the children, if any, and determines the dependency relationships of the children.
    pub async fn execute(&mut self) -> Result<(), ModelError> {
        // Relationship maps are maintained to track dependencies. A map is
        // maintained both from a Component to its dependents and from a Component to
        // that Component's dependencies. With this dependency tracking, the
        // children of the Component can be shut down progressively in dependency
        // order.
        //
        // The progressive shutdown of Component is performed in this order:
        // Note: These steps continue until the shutdown process is no longer
        // asynchronously waiting for any shut downs to complete.
        //   * Identify the one or more Component that have no dependents
        //   * A shutdown action is set to the identified components. During the
        //     shut down process, the result of the process is received
        //     asynchronously.
        //   * After a Component is shut down, the Component are removed from the list
        //     of dependents of the Component on which they had a dependency.
        //   * The list of Component is checked again to see which Component have no
        //     remaining dependents.

        // Look for any children that have no dependents
        let mut stop_targets = vec![];

        for component_ref in
            self.source_to_targets.keys().map(|key| key.clone()).collect::<Vec<_>>()
        {
            let no_dependents = {
                let info =
                    self.source_to_targets.get(&component_ref).expect("key disappeared from map");
                info.dependents.is_empty()
            };
            if no_dependents {
                stop_targets.push(
                    self.source_to_targets
                        .remove(&component_ref)
                        .expect("key disappeared from map"),
                );
            }
        }

        let mut futs = vec![];
        // Continue while we have new stop targets or unfinished futures
        while !stop_targets.is_empty() || !futs.is_empty() {
            for target in stop_targets.drain(..) {
                futs.push(Box::pin(shutdown_component(target)));
            }

            let (component_ref, _, remaining) = select_all(futs).await;
            futs = remaining;

            let component_ref = component_ref?;

            // Look up the dependencies of the component that stopped
            match self.target_to_sources.remove(&component_ref) {
                Some(sources) => {
                    for source in sources {
                        let ready_to_stop = {
                            if let Some(info) = self.source_to_targets.get_mut(&source) {
                                info.dependents.remove(&component_ref);
                                // Have all of this components dependents stopped?
                                info.dependents.is_empty()
                            } else {
                                // The component that provided a capability to
                                // the stopped component doesn't exist or
                                // somehow already stopped. This is unexpected.
                                panic!(
                                    "The component '{:?}' appears to have stopped before its \
                                     dependency '{:?}'",
                                    component_ref, source
                                );
                            }
                        };

                        // This components had zero remaining dependents
                        if ready_to_stop {
                            stop_targets.push(
                                self.source_to_targets
                                    .remove(&source)
                                    .expect("A key that was just available has disappeared."),
                            );
                        }
                    }
                }
                None => {
                    // Oh well, component didn't have any dependencies
                }
            }
        }

        // We should have stopped all children, if not probably there is a
        // dependency cycle
        if !self.source_to_targets.is_empty() {
            panic!(
                "Something failed, all children should have been removed! {:?}",
                self.source_to_targets
            );
        }
        Ok(())
    }
}

async fn do_shutdown(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    {
        let state = component.lock_state().await;
        {
            let exec_state = component.lock_execution().await;
            if exec_state.is_shut_down() {
                return Ok(());
            }
        }
        match *state {
            InstanceState::Resolved(ref s) => {
                let mut shutdown_job = ShutdownJob::new(component, s).await;
                drop(state);
                Box::pin(shutdown_job.execute()).await?;
                return Ok(());
            }
            InstanceState::New | InstanceState::Unresolved | InstanceState::Destroyed => {}
        }
    }
    // Control flow arrives here if the component isn't resolved.
    // TODO: Put this component in a "shutting down" state so that if it creates new instances
    // after this point, they are created in a shut down state.
    component.stop_instance(true, false).await?;

    Ok(())
}

/// Identifies a component in this realm. This can either be the component
/// itself, or one of its children.
#[derive(Debug, Eq, PartialEq, Hash, Clone)]
pub enum ComponentRef {
    Self_,
    Child(ChildMoniker),
}

impl From<ChildMoniker> for ComponentRef {
    fn from(moniker: ChildMoniker) -> Self {
        ComponentRef::Child(moniker)
    }
}

/// Used to track information during the shutdown process. The dependents
/// are all the component which must stop before the component represented
/// by this struct.
struct ShutdownInfo {
    // TODO(jmatt) reduce visibility of fields
    /// The identifier for this component
    pub ref_: ComponentRef,
    /// The components that this component offers capabilities to
    pub dependents: HashSet<ComponentRef>,
    pub component: Arc<ComponentInstance>,
}

impl fmt::Debug for ShutdownInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "moniker: '{:?}'", self.ref_)
    }
}

/// Trait exposing all component state necessary to compute shutdown order.
///
/// This trait largely mirrors `ComponentDecl`, but will reflect changes made to
/// the component's state at runtime (e.g., dynamically created children,
/// dynamic offers).
///
/// In production, this will probably only be implemented for
/// `ResolvedInstanceState`, but exposing this trait allows for easier testing.
pub trait Component {
    /// Current view of this component's `uses` declarations.
    fn uses(&self) -> Vec<UseDecl>;

    /// Current view of this component's `exposes` declarations.
    fn exposes(&self) -> Vec<ExposeDecl>;

    /// Current view of this component's `offers` declarations.
    fn offers(&self) -> Vec<OfferDecl>;

    /// Current view of this component's `capabilities` declarations.
    fn capabilities(&self) -> Vec<CapabilityDecl>;

    /// Current view of this component's `collections` declarations.
    fn collections(&self) -> Vec<CollectionDecl>;

    /// Current view of this component's `environments` declarations.
    fn environments(&self) -> Vec<EnvironmentDecl>;

    /// Returns metadata about each child of this component.
    fn children(&self) -> Vec<Child>;

    /// Returns the live child that has the given `name` and `collection`, or
    /// returns `None` if none match. In the case of dynamic children, it's
    /// possible for multiple children to match a given `name` and `collection`,
    /// but at most one of them can be live.
    fn find_child(&self, name: &String, collection: &Option<String>) -> Option<Child> {
        self.children().into_iter().find(|child| {
            child.moniker.name() == name
                && child.moniker.collection() == collection.as_ref().map(|s| s.as_str())
        })
    }
}

/// Child metadata necessary to compute shutdown order.
///
/// A `Component` returns information about its children by returning a vector
/// of these.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct Child {
    /// The moniker identifying the name of the child, complete with
    /// `instance_id`.
    pub moniker: ChildMoniker,

    /// Name of the environment associated with this child, if any.
    pub environment_name: Option<String>,
}

/// For a given Component, identify capability dependencies between the
/// component itself and its children. A map is returned which maps from a
/// "source" component (represented by a `ComponentRef`) to a set of "target"
/// components to which the source component provides capabilities. The targets
/// must be shut down before the source.
pub fn process_component_dependencies(
    instance: &impl Component,
) -> HashMap<ComponentRef, HashSet<ComponentRef>> {
    // We build up the set of (source, target) dependency edges from a variety
    // of sources.
    let mut edges = HashSet::new();
    edges.extend(get_dependencies_from_offers(instance).into_iter());
    edges.extend(get_dependencies_from_environments(instance).into_iter());
    edges.extend(get_dependencies_from_uses(instance).into_iter());

    // Next, we want to find any children that `self` transitively depends on,
    // either directly or through other children. Any child that `self` doesn't
    // transitively depend on implicitly depends on `self`.
    //
    // TODO(82689): This logic is likely unnecessary, as it deals with children
    // that have no direct dependency links with their parent.
    let self_dependencies_closure = dependency_closure(&edges, ComponentRef::Self_);
    let implicit_edges = instance.children().into_iter().filter_map(|child| {
        let component_ref = child.moniker.into();
        if self_dependencies_closure.contains(&component_ref) {
            None
        } else {
            Some((ComponentRef::Self_, component_ref))
        }
    });

    edges.extend(implicit_edges);

    let mut dependency_map = HashMap::new();
    dependency_map.insert(ComponentRef::Self_, HashSet::new());

    for child in instance.children() {
        dependency_map.insert(child.moniker.into(), HashSet::new());
    }

    for (source, target) in edges {
        match dependency_map.get_mut(&source) {
            Some(targets) => {
                targets.insert(target);
            }
            None => {
                error!(
                    "ignoring dependency edge from {:?} to {:?}, where source doesn't exist",
                    source, target
                );
            }
        }
    }

    dependency_map
}

/// Given a dependency graph represented as a set of `edges`, find the set of
/// all nodes that the `start` node depends on, directly or indirectly. This
/// includes `start` itself.
fn dependency_closure(
    edges: &HashSet<(ComponentRef, ComponentRef)>,
    start: ComponentRef,
) -> HashSet<ComponentRef> {
    let mut res = HashSet::new();
    res.insert(start);
    loop {
        let mut entries_added = false;

        for (source, target) in edges {
            if !res.contains(target) {
                continue;
            }
            if res.insert(source.clone()) {
                entries_added = true
            }
        }
        if !entries_added {
            return res;
        }
    }
}

/// Return the set of dependency relationships that can be derived from the
/// component's use declarations. For use declarations, `self` is always the
/// target.
fn get_dependencies_from_uses(instance: &impl Component) -> HashSet<(ComponentRef, ComponentRef)> {
    let mut edges = HashSet::new();
    for use_ in &instance.uses() {
        let child_name = match use_ {
            UseDecl::Service(UseServiceDecl { source: UseSource::Child(name), .. })
            | UseDecl::Protocol(UseProtocolDecl { source: UseSource::Child(name), .. })
            | UseDecl::Directory(UseDirectoryDecl { source: UseSource::Child(name), .. })
            | UseDecl::Event(UseEventDecl { source: UseSource::Child(name), .. }) => name,
            UseDecl::Service(_)
            | UseDecl::Protocol(_)
            | UseDecl::Directory(_)
            | UseDecl::Event(_)
            | UseDecl::Storage(_)
            | UseDecl::EventStreamDeprecated(_) => {
                // capabilities which cannot or are not used from a child can be ignored.
                continue;
            }
            UseDecl::EventStream(_) => {
                continue;
            }
        };
        match use_ {
            UseDecl::Protocol(UseProtocolDecl { dependency_type, .. })
            | UseDecl::Service(UseServiceDecl { dependency_type, .. })
            | UseDecl::Directory(UseDirectoryDecl { dependency_type, .. })
            | UseDecl::Event(UseEventDecl { dependency_type, .. }) => {
                if dependency_type == &DependencyType::Weak
                    || dependency_type == &DependencyType::WeakForMigration
                {
                    // Weak dependencies are ignored when determining shutdown ordering
                    continue;
                }
            }
            UseDecl::Storage(_) | UseDecl::EventStreamDeprecated(_) | UseDecl::EventStream(_) => {
                // Any other capability type cannot be marked as weak, so we can proceed
            }
        }

        let child = match instance.find_child(child_name, &None) {
            Some(child) => child.moniker.clone().into(),
            None => {
                error!(name=?child_name, "use source doesn't exist");
                continue;
            }
        };

        edges.insert((child, ComponentRef::Self_));
    }
    edges
}

/// Return the set of dependency relationships that can be derived from the
/// component's offer declarations. This includes both static and dynamic offers.
fn get_dependencies_from_offers(
    instance: &impl Component,
) -> HashSet<(ComponentRef, ComponentRef)> {
    let mut edges = HashSet::new();

    for offer_decl in instance.offers() {
        if let Some((sources, targets)) = get_dependency_from_offer(instance, &offer_decl) {
            for source in sources.iter() {
                for target in targets.iter() {
                    edges.insert((source.clone(), target.clone()));
                }
            }
        }
    }
    edges
}

/// Extracts a list of sources and a list of targets from a single `OfferDecl`,
/// or returns `None` if the offer has no impact on shutdown ordering. The
/// `Component` provides context that may be necessary to understand the
/// `OfferDecl`. Note that a single offer can have multiple sources/targets; for
/// instance, targeting a collection targets all the children within that
/// collection.
fn get_dependency_from_offer(
    instance: &impl Component,
    offer_decl: &OfferDecl,
) -> Option<(Vec<ComponentRef>, Vec<ComponentRef>)> {
    // We only care about dependencies where the provider of the dependency is
    // `self` or another child, otherwise the capability comes from the parent
    // or component manager itself in which case the relationship is not
    // relevant for ordering here.
    match offer_decl {
        OfferDecl::Protocol(OfferProtocolDecl {
            dependency_type: DependencyType::Strong,
            source,
            target,
            ..
        })
        | OfferDecl::Directory(OfferDirectoryDecl {
            dependency_type: DependencyType::Strong,
            source,
            target,
            ..
        })
        | OfferDecl::Runner(OfferRunnerDecl { source, target, .. })
        | OfferDecl::Resolver(OfferResolverDecl { source, target, .. }) => {
            Some((find_offer_sources(instance, source), find_offer_targets(instance, target)))
        }

        OfferDecl::Service(OfferServiceDecl { source, target, .. }) => Some((
            find_service_offer_sources(instance, source),
            find_offer_targets(instance, target),
        )),

        OfferDecl::Protocol(OfferProtocolDecl {
            dependency_type: DependencyType::Weak | DependencyType::WeakForMigration,
            ..
        })
        | OfferDecl::Directory(OfferDirectoryDecl {
            dependency_type: DependencyType::Weak | DependencyType::WeakForMigration,
            ..
        }) => {
            // weak dependencies are ignored by this algorithm, because weak
            // dependencies can be broken arbitrarily.
            None
        }

        // Storage is special.
        OfferDecl::Storage(OfferStorageDecl {
            source: OfferSource::Self_,
            source_name,
            target,
            ..
        }) => {
            Some((find_storage_source(instance, source_name), find_offer_targets(instance, target)))
        }
        OfferDecl::Storage(OfferStorageDecl {
            source:
                OfferSource::Child(_)
                | OfferSource::Parent
                | OfferSource::Capability(_)
                | OfferSource::Collection(_)
                | OfferSource::Framework
                | OfferSource::Void,
            ..
        }) => {
            // The storage offer is not from `self`, so it can be ignored.
            //
            // NOTE: It may seem weird that storage offers from "child" are
            // ignored, but storage offers that come from children work
            // differently than other kinds of offers. A Storage offer always
            // comes from either `parent` or `self`, but the storage capability
            // _itself_ (see `StorageDecl`) may reference a child. But in that
            // case, the `OfferSource` is still listed as `self`, in which case
            // it is handled above.
            None
        }

        OfferDecl::Event(_) => {
            // Events aren't tracked as dependencies for shutdown.
            None
        }
        OfferDecl::EventStream(_) => {
            // Event streams aren't tracked as dependencies for shutdown.
            None
        }
    }
}

fn find_service_offer_sources(
    instance: &impl Component,
    source: &OfferSource,
) -> Vec<ComponentRef> {
    // if the offer source is a collection, collect all children in the
    // collection, otherwise defer to the "regular" method for this
    match source {
        OfferSource::Collection(collection_name) => instance
            .children()
            .into_iter()
            .filter_map(|child| {
                if let Some(child_collection) = child.moniker.collection() {
                    if child_collection == collection_name {
                        Some(child.moniker.clone().into())
                    } else {
                        None
                    }
                } else {
                    None
                }
            })
            .collect(),
        _ => find_offer_sources(instance, source),
    }
}

/// Given a `Component` instance and an `OfferSource`, return the names of
/// components that match that `source`.
fn find_offer_sources(instance: &impl Component, source: &OfferSource) -> Vec<ComponentRef> {
    match source {
        OfferSource::Child(ChildRef { name, collection }) => {
            match instance.find_child(name, collection) {
                Some(child) => vec![child.moniker.clone().into()],
                None => {
                    error!(
                        "offer source doesn't exist: (name: {:?}, collection: {:?})",
                        name, collection
                    );
                    vec![]
                }
            }
        }
        OfferSource::Self_ => vec![ComponentRef::Self_],
        OfferSource::Collection(_) => {
            // TODO(fxbug.dev/84766): Consider services routed from collections
            // in shutdown order.
            vec![]
        }
        OfferSource::Parent | OfferSource::Framework => {
            // Capabilities offered by the parent or provided by the framework
            // (based on some other capability) are not relevant.
            vec![]
        }
        OfferSource::Capability(_) => {
            // OfferSource::Capability(_) is used for the `StorageAdmin`
            // capability. This capability is implemented by component_manager,
            // and is therefore similar to a Framework capability, but it also
            // depends on the underlying storage that's being administrated. In
            // theory, we could add an edge from the source of that storage to
            // the target of the`StorageAdmin` capability... but honestly it's
            // very complex and confusing and doesn't seem worth it.
            //
            // We may want to reconsider this someday.
            vec![]
        }
        OfferSource::Void => {
            // Offer sources that are intentionally omitted will never match any components
            vec![]
        }
    }
}

/// Given a `Component` and the name of a storage capability, return the names
/// of components that act as a source for that storage.
///
/// The return value will have at most one entry in it, but it is returned in a
/// Vec for consistency with the other `find_*` methods.
fn find_storage_source(instance: &impl Component, name: &CapabilityName) -> Vec<ComponentRef> {
    let decl = instance.capabilities().into_iter().find_map(|decl| match decl {
        CapabilityDecl::Storage(decl) if &decl.name == name => Some(decl),
        _ => None,
    });

    let decl = match decl {
        Some(d) => d,
        None => {
            error!(?name, "could not find storage capability");
            return vec![];
        }
    };

    match decl.source {
        StorageDirectorySource::Child(child_name) => {
            match instance.find_child(&child_name, &None) {
                Some(child) => vec![child.moniker.clone().into()],
                None => {
                    error!(
                        "source for storage capability {:?} doesn't exist: (name: {:?})",
                        name, child_name,
                    );
                    vec![]
                }
            }
        }
        StorageDirectorySource::Self_ => vec![ComponentRef::Self_],

        // Storage from the parent is not relevant to shutdown order.
        StorageDirectorySource::Parent => vec![],
    }
}

/// Given a `Component` instance and an `OfferTarget`, return the names of
/// components that match that `target`.
fn find_offer_targets(instance: &impl Component, target: &OfferTarget) -> Vec<ComponentRef> {
    match target {
        OfferTarget::Child(ChildRef { name, collection }) => {
            match instance.find_child(name, collection) {
                Some(child) => vec![child.moniker.into()],
                None => {
                    error!(
                        "offer target doesn't exist: (name: {:?}, collection: {:?})",
                        name, collection
                    );
                    vec![]
                }
            }
        }
        OfferTarget::Collection(collection) => instance
            .children()
            .into_iter()
            .filter(|child| child.moniker.collection() == Some(collection))
            .map(|child| child.moniker.into())
            .collect(),
    }
}

/// Return the set of dependency relationships that can be derived from the
/// component's environment configuration. Children assigned to an environment
/// depend on components that contribute to that environment.
fn get_dependencies_from_environments(
    instance: &impl Component,
) -> HashSet<(ComponentRef, ComponentRef)> {
    let env_to_sources: HashMap<String, HashSet<ComponentRef>> = instance
        .environments()
        .into_iter()
        .map(|env| {
            let sources = find_environment_sources(instance, &env);
            (env.name, sources)
        })
        .collect();

    let mut res = HashSet::new();
    for child in &instance.children() {
        if let Some(env_name) = &child.environment_name {
            if let Some(source_children) = env_to_sources.get(env_name) {
                for source in source_children {
                    res.insert((source.clone(), child.moniker.clone().into()));
                }
            } else {
                error!(
                    "environment `{}` from child `{}` is not a valid environment",
                    env_name, child.moniker
                )
            }
        }
    }
    res
}

/// Given a `Component` instance and an environment, return the names of
/// components that provide runners, resolvers, or debug_capabilities for that
/// environment.
fn find_environment_sources(
    instance: &impl Component,
    env: &EnvironmentDecl,
) -> HashSet<ComponentRef> {
    // Get all the `RegistrationSources` for the runners, resolvers, and
    // debug_capabilities in this environment.
    let registration_sources = env
        .runners
        .iter()
        .map(|r| &r.source)
        .chain(env.resolvers.iter().map(|r| &r.source))
        .chain(env.debug_capabilities.iter().map(|r| r.source()));

    // Turn the shutdown-relevant sources into `ComponentRef`s.
    registration_sources
        .flat_map(|source| match source {
            RegistrationSource::Self_ => vec![ComponentRef::Self_],
            RegistrationSource::Child(child_name) => {
                match instance.find_child(&child_name, &None) {
                    Some(child) => vec![child.moniker.into()],
                    None => {
                        error!(
                            "source for environment {:?} doesn't exist: (name: {:?})",
                            env.name, child_name,
                        );
                        vec![]
                    }
                }
            }
            RegistrationSource::Parent => vec![],
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            actions::{
                test_utils::{is_executing, is_unresolved},
                StopAction,
            },
            component::StartReason,
            hooks::{self, EventPayload, EventType, Hook, HooksRegistration},
            starter::Starter,
            testing::{
                test_helpers::{
                    component_decl_with_test_runner, default_component_decl,
                    execution_is_shut_down, has_child, ActionsTest, ComponentInfo,
                },
                test_hook::Lifecycle,
            },
        },
        async_trait::async_trait,
        cm_rust::{
            Availability, CapabilityName, CapabilityPath, ChildDecl, ComponentDecl, DependencyType,
            ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget, OfferDecl,
            OfferProtocolDecl, OfferResolverDecl, OfferSource, OfferStorageDecl, OfferTarget,
            ProtocolDecl, StorageDecl, StorageDirectorySource, UseDecl, UseSource,
        },
        cm_rust_testing::{
            ChildDeclBuilder, CollectionDeclBuilder, ComponentDeclBuilder, EnvironmentDeclBuilder,
        },
        cm_types::AllowedOffers,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
        maplit::{btreeset, hashmap, hashset},
        moniker::{AbsoluteMoniker, ChildMoniker},
        std::collections::{BTreeSet, HashMap},
        std::{convert::TryFrom, sync::Weak},
        test_case::test_case,
    };

    /// Implementation of `super::Component` based on a `ComponentDecl` and
    /// minimal information about runtime state.
    struct FakeComponent {
        decl: ComponentDecl,
        dynamic_children: Vec<Child>,
        dynamic_offers: Vec<OfferDecl>,
    }

    impl FakeComponent {
        /// Returns a `FakeComponent` with no dynamic children or offers.
        fn from_decl(decl: ComponentDecl) -> Self {
            Self { decl, dynamic_children: vec![], dynamic_offers: vec![] }
        }
    }

    impl Component for FakeComponent {
        fn uses(&self) -> Vec<UseDecl> {
            self.decl.uses.clone()
        }

        fn exposes(&self) -> Vec<ExposeDecl> {
            self.decl.exposes.clone()
        }

        fn offers(&self) -> Vec<OfferDecl> {
            self.decl.offers.iter().cloned().chain(self.dynamic_offers.iter().cloned()).collect()
        }

        fn capabilities(&self) -> Vec<CapabilityDecl> {
            self.decl.capabilities.clone()
        }

        fn collections(&self) -> Vec<cm_rust::CollectionDecl> {
            self.decl.collections.clone()
        }

        fn environments(&self) -> Vec<cm_rust::EnvironmentDecl> {
            self.decl.environments.clone()
        }

        fn children(&self) -> Vec<Child> {
            self.decl
                .children
                .iter()
                .map(|c| Child {
                    moniker: ChildMoniker::try_new(&c.name, None)
                        .expect("children should have valid monikers"),
                    environment_name: c.environment.clone(),
                })
                .chain(self.dynamic_children.iter().cloned())
                .collect()
        }
    }

    // TODO(jmatt) Add tests for all capability types

    /// Returns a `ComponentRef` for a child by parsing the moniker. Panics if
    /// the moniker is malformed.
    fn child(moniker: &str) -> ComponentRef {
        ChildMoniker::from(moniker).into()
    }

    #[fuchsia::test]
    fn test_service_from_self() {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Self_,
                source_name: "serviceSelf".into(),
                target_name: "serviceSelf".into(),
                target: OfferTarget::static_child("childA".to_string()),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fdecl::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA")],
                child("childA") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[test_case(DependencyType::Weak)]
    #[test_case(DependencyType::WeakForMigration)]
    fn test_weak_service_from_self(weak_dep: DependencyType) {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Self_,
                source_name: "serviceSelf".into(),
                target_name: "serviceSelf".into(),
                target: OfferTarget::static_child("childA".to_string()),
                dependency_type: weak_dep,
                availability: Availability::Required,
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fdecl::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA")],
                child("childA") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_service_from_child() {
        let decl = ComponentDecl {
            exposes: vec![ExposeDecl::Protocol(ExposeProtocolDecl {
                target: ExposeTarget::Parent,
                source_name: "serviceFromChild".into(),
                target_name: "serviceFromChild".into(),
                source: ExposeSource::Child("childA".to_string()),
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fdecl::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA")],
                child("childA") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_single_dependency() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Self_,
                    source_name: "serviceParent".into(),
                    target_name: "serviceParent".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBOffer".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![],
                child("childB") => hashset![child("childA")],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_runner_from_parent() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("env")
                .add_runner(cm_rust::RunnerRegistration {
                    source: RegistrationSource::Parent,
                    source_name: "foo".into(),
                    target_name: "foo".into(),
                })
                .build()],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("env").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_runner_from_self() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("env")
                .add_runner(cm_rust::RunnerRegistration {
                    source: RegistrationSource::Self_,
                    source_name: "foo".into(),
                    target_name: "foo".into(),
                })
                .build()],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("env").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_runner_from_child() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("env")
                .add_runner(cm_rust::RunnerRegistration {
                    source: RegistrationSource::Child("childA".to_string()),
                    source_name: "foo".into(),
                    target_name: "foo".into(),
                })
                .build()],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("env").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![child("childB")],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_runner_from_child_to_collection() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("env")
                .add_runner(cm_rust::RunnerRegistration {
                    source: RegistrationSource::Child("childA".to_string()),
                    source_name: "foo".into(),
                    target_name: "foo".into(),
                })
                .build()],
            collections: vec![CollectionDeclBuilder::new().name("coll").environment("env").build()],
            children: vec![ChildDeclBuilder::new_lazy_child("childA").build()],
            ..default_component_decl()
        };

        let instance = FakeComponent {
            decl,
            dynamic_children: vec![
                // NOTE: The environment must be set in the `Child`, even though
                // it can theoretically be inferred from the collection
                // declaration.
                Child { moniker: "coll:dyn1".into(), environment_name: Some("env".to_string()) },
                Child { moniker: "coll:dyn2".into(), environment_name: Some("env".to_string()) },
            ],
            dynamic_offers: vec![],
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                ],
                child("childA") => hashset![
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                ],
                child("coll:dyn1") => hashset![],
                child("coll:dyn2") => hashset![],
            },
            process_component_dependencies(&instance)
        )
    }

    #[fuchsia::test]
    fn test_chained_environments() {
        let decl = ComponentDecl {
            environments: vec![
                EnvironmentDeclBuilder::new()
                    .name("env")
                    .add_runner(cm_rust::RunnerRegistration {
                        source: RegistrationSource::Child("childA".to_string()),
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                    })
                    .build(),
                EnvironmentDeclBuilder::new()
                    .name("env2")
                    .add_runner(cm_rust::RunnerRegistration {
                        source: RegistrationSource::Child("childB".to_string()),
                        source_name: "bar".into(),
                        target_name: "bar".into(),
                    })
                    .build(),
            ],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("env").build(),
                ChildDeclBuilder::new_lazy_child("childC").environment("env2").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("childC")
                ],
                child("childA") => hashset![child("childB")],
                child("childB") => hashset![child("childC")],
                child("childC") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_and_offer() {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::static_child("childB".to_string()),
                source_name: "childBOffer".into(),
                target_name: "serviceSibling".into(),
                target: OfferTarget::static_child("childC".to_string()),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            environments: vec![EnvironmentDeclBuilder::new()
                .name("env")
                .add_runner(cm_rust::RunnerRegistration {
                    source: RegistrationSource::Child("childA".into()),
                    source_name: "foo".into(),
                    target_name: "foo".into(),
                })
                .build()],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("env").build(),
                ChildDeclBuilder::new_lazy_child("childC").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("childC")
                ],
                child("childA") => hashset![child("childB")],
                child("childB") => hashset![child("childC")],
                child("childC") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_resolver_from_parent() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("resolver_env")
                .add_resolver(cm_rust::ResolverRegistration {
                    source: RegistrationSource::Parent,
                    resolver: "foo".into(),
                    scheme: "httweeeeees".into(),
                })
                .build()],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("resolver_env").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_resolver_from_child() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("resolver_env")
                .add_resolver(cm_rust::ResolverRegistration {
                    source: RegistrationSource::Child("childA".to_string()),
                    resolver: "foo".into(),
                    scheme: "httweeeeees".into(),
                })
                .build()],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("resolver_env").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![child("childB")],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    // add test where B depends on A via environment and C depends on B via environment

    #[fuchsia::test]
    fn test_environment_with_chain_of_resolvers() {
        let decl = ComponentDecl {
            environments: vec![
                EnvironmentDeclBuilder::new()
                    .name("env1")
                    .add_resolver(cm_rust::ResolverRegistration {
                        source: RegistrationSource::Child("childA".to_string()),
                        resolver: "foo".into(),
                        scheme: "httweeeeees".into(),
                    })
                    .build(),
                EnvironmentDeclBuilder::new()
                    .name("env2")
                    .add_resolver(cm_rust::ResolverRegistration {
                        source: RegistrationSource::Child("childB".to_string()),
                        resolver: "bar".into(),
                        scheme: "httweeeeee".into(),
                    })
                    .build(),
            ],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").environment("env1").build(),
                ChildDeclBuilder::new_lazy_child("childC").environment("env2").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("childC")
                ],
                child("childA") => hashset![child("childB")],
                child("childB") => hashset![child("childC")],
                child("childC") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_resolver_and_runner_from_child() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("multi_env")
                .add_resolver(cm_rust::ResolverRegistration {
                    source: RegistrationSource::Child("childA".to_string()),
                    resolver: "foo".into(),
                    scheme: "httweeeeees".into(),
                })
                .add_runner(cm_rust::RunnerRegistration {
                    source: RegistrationSource::Child("childB".to_string()),
                    source_name: "bar".into(),
                    target_name: "bar".into(),
                })
                .build()],
            children: vec![
                ChildDeclBuilder::new_lazy_child("childA").build(),
                ChildDeclBuilder::new_lazy_child("childB").build(),
                ChildDeclBuilder::new_lazy_child("childC").environment("multi_env").build(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("childC")
                ],
                child("childA") => hashset![child("childC")],
                child("childB") => hashset![child("childC")],
                child("childC") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_environment_with_collection_resolver_from_child() {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("resolver_env")
                .add_resolver(cm_rust::ResolverRegistration {
                    source: RegistrationSource::Child("childA".to_string()),
                    resolver: "foo".into(),
                    scheme: "httweeeeees".into(),
                })
                .build()],
            children: vec![ChildDeclBuilder::new_lazy_child("childA").build()],
            collections: vec![CollectionDeclBuilder::new()
                .name("coll")
                .environment("resolver_env")
                .build()],
            ..default_component_decl()
        };

        let instance = FakeComponent {
            decl,
            dynamic_children: vec![
                // NOTE: The environment must be set in the `Child`, even though
                // it can theoretically be inferred from the collection declaration.
                Child {
                    moniker: "coll:dyn1".into(),
                    environment_name: Some("resolver_env".to_string()),
                },
                Child {
                    moniker: "coll:dyn2".into(),
                    environment_name: Some("resolver_env".to_string()),
                },
            ],
            dynamic_offers: vec![],
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                ],
                child("childA") => hashset![child("coll:dyn1"), child("coll:dyn2")],
                child("coll:dyn1") => hashset![],
                child("coll:dyn2") => hashset![],
            },
            process_component_dependencies(&instance)
        )
    }

    #[fuchsia::test]
    fn test_dynamic_offers_within_collection() {
        let decl = ComponentDeclBuilder::new()
            .add_lazy_child("childA")
            .add_transient_collection("coll")
            .offer(OfferDecl::Directory(OfferDirectoryDecl {
                source: OfferSource::Child(ChildRef {
                    name: "childA".to_string(),
                    collection: None,
                }),
                target: OfferTarget::Collection("coll".to_string()),
                source_name: "some_dir".into(),
                target_name: "some_dir".into(),
                dependency_type: DependencyType::Strong,
                rights: None,
                subdir: None,
                availability: Availability::Required,
            }))
            .build();

        let instance = FakeComponent {
            decl,
            dynamic_children: vec![
                Child { moniker: "coll:dyn1".into(), environment_name: None },
                Child { moniker: "coll:dyn2".into(), environment_name: None },
                Child { moniker: "coll:dyn3".into(), environment_name: None },
                Child { moniker: "coll:dyn4".into(), environment_name: None },
            ],
            dynamic_offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Child(ChildRef {
                        name: "dyn1".to_string(),
                        collection: Some("coll".to_string()),
                    }),
                    target: OfferTarget::Child(ChildRef {
                        name: "dyn2".to_string(),
                        collection: Some("coll".to_string()),
                    }),
                    source_name: "test.protocol".into(),
                    target_name: "test.protocol".into(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Child(ChildRef {
                        name: "dyn1".to_string(),
                        collection: Some("coll".to_string()),
                    }),
                    target: OfferTarget::Child(ChildRef {
                        name: "dyn3".to_string(),
                        collection: Some("coll".to_string()),
                    }),
                    source_name: "test.protocol".into(),
                    target_name: "test.protocol".into(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
            ],
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                    child("coll:dyn3"),
                    child("coll:dyn4"),
                ],
                child("childA") => hashset![
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                    child("coll:dyn3"),
                    child("coll:dyn4"),
                ],
                child("coll:dyn1") => hashset![child("coll:dyn2"), child("coll:dyn3")],
                child("coll:dyn2") => hashset![],
                child("coll:dyn3") => hashset![],
                child("coll:dyn4") => hashset![],
            },
            process_component_dependencies(&instance)
        )
    }

    #[fuchsia::test]
    fn test_dynamic_offers_between_collections() {
        let decl = ComponentDeclBuilder::new()
            .add_transient_collection("coll1")
            .add_transient_collection("coll2")
            .build();

        let instance = FakeComponent {
            decl,
            dynamic_children: vec![
                Child { moniker: "coll1:dyn1".into(), environment_name: None },
                Child { moniker: "coll1:dyn2".into(), environment_name: None },
                Child { moniker: "coll2:dyn1".into(), environment_name: None },
                Child { moniker: "coll2:dyn2".into(), environment_name: None },
            ],
            dynamic_offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Child(ChildRef {
                        name: "dyn1".to_string(),
                        collection: Some("coll1".to_string()),
                    }),
                    target: OfferTarget::Child(ChildRef {
                        name: "dyn1".to_string(),
                        collection: Some("coll2".to_string()),
                    }),
                    source_name: "test.protocol".into(),
                    target_name: "test.protocol".into(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Child(ChildRef {
                        name: "dyn2".to_string(),
                        collection: Some("coll2".to_string()),
                    }),
                    target: OfferTarget::Child(ChildRef {
                        name: "dyn1".to_string(),
                        collection: Some("coll1".to_string()),
                    }),
                    source_name: "test.protocol".into(),
                    target_name: "test.protocol".into(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
            ],
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("coll1:dyn1"),
                    child("coll1:dyn2"),
                    child("coll2:dyn1"),
                    child("coll2:dyn2"),
                ],
                child("coll1:dyn1") => hashset![child("coll2:dyn1")],
                child("coll1:dyn2") => hashset![],
                child("coll2:dyn1") => hashset![],
                child("coll2:dyn2") => hashset![child("coll1:dyn1")],
            },
            process_component_dependencies(&instance)
        )
    }

    #[fuchsia::test]
    fn test_dynamic_offer_from_parent() {
        let decl = ComponentDeclBuilder::new().add_transient_collection("coll").build();
        let instance = FakeComponent {
            decl,
            dynamic_children: vec![
                Child { moniker: "coll:dyn1".into(), environment_name: None },
                Child { moniker: "coll:dyn2".into(), environment_name: None },
            ],
            dynamic_offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Parent,
                target: OfferTarget::Child(ChildRef {
                    name: "dyn1".to_string(),
                    collection: Some("coll".to_string()),
                }),
                source_name: "test.protocol".into(),
                target_name: "test.protocol".into(),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                ],
                child("coll:dyn1") => hashset![],
                child("coll:dyn2") => hashset![],
            },
            process_component_dependencies(&instance)
        )
    }

    #[fuchsia::test]
    fn test_dynamic_offer_from_self() {
        let decl = ComponentDeclBuilder::new().add_transient_collection("coll").build();
        let instance = FakeComponent {
            decl,
            dynamic_children: vec![
                Child { moniker: "coll:dyn1".into(), environment_name: None },
                Child { moniker: "coll:dyn2".into(), environment_name: None },
            ],
            dynamic_offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Self_,
                target: OfferTarget::Child(ChildRef {
                    name: "dyn1".to_string(),
                    collection: Some("coll".to_string()),
                }),
                source_name: "test.protocol".into(),
                target_name: "test.protocol".into(),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                ],
                child("coll:dyn1") => hashset![],
                child("coll:dyn2") => hashset![],
            },
            process_component_dependencies(&instance)
        )
    }

    #[fuchsia::test]
    fn test_dynamic_offer_from_static_child() {
        let decl = ComponentDeclBuilder::new()
            .add_lazy_child("childA")
            .add_lazy_child("childB")
            .add_transient_collection("coll")
            .build();

        let instance = FakeComponent {
            decl,
            dynamic_children: vec![
                Child { moniker: "coll:dyn1".into(), environment_name: None },
                Child { moniker: "coll:dyn2".into(), environment_name: None },
            ],
            dynamic_offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Child(ChildRef {
                    name: "childA".to_string(),
                    collection: None,
                }),
                target: OfferTarget::Child(ChildRef {
                    name: "dyn1".to_string(),
                    collection: Some("coll".to_string()),
                }),
                source_name: "test.protocol".into(),
                target_name: "test.protocol".into(),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("coll:dyn1"),
                    child("coll:dyn2"),
                ],
                child("childA") => hashset![child("coll:dyn1")],
                child("childB") => hashset![],
                child("coll:dyn1") => hashset![],
                child("coll:dyn2") => hashset![],
            },
            process_component_dependencies(&instance)
        )
    }

    #[test_case(DependencyType::Weak)]
    #[test_case(DependencyType::WeakForMigration)]
    fn test_single_weak_dependency(weak_dep: DependencyType) {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Self_,
                    source_name: "serviceSelf".into(),
                    target_name: "serviceSelf".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: weak_dep.clone(),
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBOffer".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: weak_dep.clone(),
                    availability: Availability::Required,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_multiple_dependencies_same_source() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Self_,
                    source_name: "serviceSelf".into(),
                    target_name: "serviceSelf".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBOffer".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBOtherOffer".into(),
                    target_name: "serviceOtherSibling".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![],
                child("childB") => hashset![child("childA")],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_multiple_dependents_same_source() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBOffer".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBToC".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone(), child_c.clone()],

            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("childC"),
                ],
                child("childA") => hashset![],
                child("childB") => hashset![child("childA"), child("childC")],
                child("childC") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[test_case(DependencyType::Weak)]
    #[test_case(DependencyType::WeakForMigration)]
    fn test_multiple_dependencies(weak_dep: DependencyType) {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childA".to_string()),
                    source_name: "childBOffer".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBToC".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childC".to_string()),
                    source_name: "childCToA".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    dependency_type: weak_dep,
                    availability: Availability::Required,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone(), child_c.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("childC"),
                ],
                child("childA") => hashset![child("childC")],
                child("childB") => hashset![child("childC")],
                child("childC") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_component_is_source_and_target() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childA".to_string()),
                    source_name: "childBOffer".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childB".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBToC".into(),
                    target_name: "serviceSibling".into(),
                    target: OfferTarget::static_child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone(), child_c.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    child("childA"),
                    child("childB"),
                    child("childC"),
                ],
                child("childA") => hashset![child("childB")],
                child("childB") => hashset![child("childC")],
                child("childC") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    /// Tests a graph that looks like the below, tildes indicate a
    /// capability route. Route point toward the target of the capability
    /// offer. The manifest constructed is for 'P'.
    ///       P
    ///    ___|___
    ///  /  / | \  \
    /// e<~c<~a~>b~>d
    ///     \      /
    ///      *>~~>*
    #[fuchsia::test]
    fn test_complex_routing() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_d = ChildDecl {
            name: "childD".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_e = ChildDecl {
            name: "childE".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childA".to_string()),
                    source_name: "childAService".into(),
                    target_name: "childAService".into(),
                    target: OfferTarget::static_child("childB".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childA".to_string()),
                    source_name: "childAService".into(),
                    target_name: "childAService".into(),
                    target: OfferTarget::static_child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childB".to_string()),
                    source_name: "childBService".into(),
                    target_name: "childBService".into(),
                    target: OfferTarget::static_child("childD".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childC".to_string()),
                    source_name: "childAService".into(),
                    target_name: "childAService".into(),
                    target: OfferTarget::static_child("childD".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::static_child("childC".to_string()),
                    source_name: "childAService".into(),
                    target_name: "childAService".into(),
                    target: OfferTarget::static_child("childE".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
            ],
            children: vec![
                child_a.clone(),
                child_b.clone(),
                child_c.clone(),
                child_d.clone(),
                child_e.clone(),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                   child("childA"),
                   child("childB"),
                   child("childC"),
                   child("childD"),
                   child("childE"),
                ],
                child("childA") => hashset![child("childB"), child("childC")],
                child("childB") => hashset![child("childD")],
                child("childC") => hashset![child("childD"), child("childE")],
                child("childD") => hashset![],
                child("childE") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_target_does_not_exist() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        // This declaration is invalid because the offer target doesn't exist
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::static_child("childA".to_string()),
                source_name: "childBOffer".into(),
                target_name: "serviceSibling".into(),
                target: OfferTarget::static_child("childB".to_string()),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            children: vec![child_a.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA")],
                child("childA") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        );
    }

    #[fuchsia::test]
    fn test_service_from_collection() {
        let decl = ComponentDecl {
            collections: vec![CollectionDecl {
                name: "coll".to_string(),
                durability: fdecl::Durability::Transient,
                environment: None,
                allowed_offers: cm_types::AllowedOffers::StaticOnly,
                allow_long_names: false,
                persistent_storage: Some(false),
            }],
            children: vec![ChildDecl {
                name: "static_child".into(),
                url: "fuchsia-pkg://imaginary".to_string(),
                startup: fdecl::StartupMode::Lazy,
                on_terminate: None,
                environment: None,
            }],
            offers: vec![OfferDecl::Service(OfferServiceDecl {
                source: OfferSource::Collection("coll".to_string()),
                source_name: "service_capability".into(),
                target: OfferTarget::Child(ChildRef {
                    name: "static_child".to_string(),
                    collection: None,
                }),
                target_name: "service_capbility".into(),
                source_instance_filter: None,
                renamed_instances: None,
                availability: Availability::Required,
            })],
            ..default_component_decl()
        };

        let dynamic_child = ChildMoniker::try_new("dynamic_child", Some("coll")).unwrap();
        let mut fake = FakeComponent::from_decl(decl);
        fake.dynamic_children
            .push(Child { moniker: dynamic_child.clone(), environment_name: None });

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    ComponentRef::Child(dynamic_child.clone()),child("static_child")
                ],
                ComponentRef::Child(dynamic_child) => hashset![child("static_child")],
                child("static_child") => hashset![],
            },
            process_component_dependencies(&fake)
        );
    }

    #[fuchsia::test]
    fn test_service_from_collection_with_multiple_instances() {
        let decl = ComponentDecl {
            collections: vec![CollectionDecl {
                name: "coll".to_string(),
                durability: fdecl::Durability::Transient,
                environment: None,
                allowed_offers: cm_types::AllowedOffers::StaticOnly,
                allow_long_names: false,
                persistent_storage: Some(false),
            }],
            children: vec![ChildDecl {
                name: "static_child".into(),
                url: "fuchsia-pkg://imaginary".to_string(),
                startup: fdecl::StartupMode::Lazy,
                on_terminate: None,
                environment: None,
            }],
            offers: vec![OfferDecl::Service(OfferServiceDecl {
                source: OfferSource::Collection("coll".to_string()),
                source_name: "service_capability".into(),
                target: OfferTarget::Child(ChildRef {
                    name: "static_child".to_string(),
                    collection: None,
                }),
                target_name: "service_capbility".into(),
                source_instance_filter: None,
                renamed_instances: None,
                availability: Availability::Required,
            })],
            ..default_component_decl()
        };

        let dynamic_child1 = ChildMoniker::try_new("dynamic_child1", Some("coll")).unwrap();
        let dynamic_child2 = ChildMoniker::try_new("dynamic_child2", Some("coll")).unwrap();
        let mut fake = FakeComponent::from_decl(decl);
        fake.dynamic_children
            .push(Child { moniker: dynamic_child1.clone(), environment_name: None });
        fake.dynamic_children
            .push(Child { moniker: dynamic_child2.clone(), environment_name: None });

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![
                    ComponentRef::Child(dynamic_child1.clone()),
                    ComponentRef::Child(dynamic_child2.clone()),
                    child("static_child")
                ],
                ComponentRef::Child(dynamic_child1) => hashset![child("static_child")],
                ComponentRef::Child(dynamic_child2) => hashset![child("static_child")],
                child("static_child") => hashset![],
            },
            process_component_dependencies(&fake)
        );
    }

    #[fuchsia::test]
    fn test_service_dependency_between_collections() {
        let c1_name = "coll1".to_string();
        let c2_name = "coll2".to_string();
        let cap_name = "fuchsia.service.FakeService".to_string();
        let decl = ComponentDecl {
            collections: vec![
                CollectionDecl {
                    name: c1_name.clone(),
                    durability: fdecl::Durability::Transient,
                    environment: None,
                    allowed_offers: cm_types::AllowedOffers::StaticOnly,
                    allow_long_names: false,
                    persistent_storage: Some(false),
                },
                CollectionDecl {
                    name: c2_name.clone(),
                    durability: fdecl::Durability::Transient,
                    environment: None,
                    allowed_offers: cm_types::AllowedOffers::StaticOnly,
                    allow_long_names: false,
                    persistent_storage: Some(false),
                },
            ],
            offers: vec![OfferDecl::Service(OfferServiceDecl {
                source: OfferSource::Collection(c1_name.clone()),
                source_name: cap_name.clone().into(),
                target: OfferTarget::Collection(c2_name.clone()),
                target_name: cap_name.clone().into(),
                source_instance_filter: None,
                renamed_instances: None,
                availability: Availability::Required,
            })],
            ..default_component_decl()
        };

        let source_child1 = ChildMoniker::try_new("source_child1", Some(&c1_name)).unwrap();
        let source_child2 = ChildMoniker::try_new("source_child2", Some(&c1_name)).unwrap();
        let target_child1 = ChildMoniker::try_new("target_child1", Some(&c2_name)).unwrap();
        let target_child2 = ChildMoniker::try_new("target_child2", Some(&c2_name)).unwrap();

        let mut fake = FakeComponent::from_decl(decl);
        fake.dynamic_children
            .push(Child { moniker: source_child1.clone(), environment_name: None });
        fake.dynamic_children
            .push(Child { moniker: source_child2.clone(), environment_name: None });
        fake.dynamic_children
            .push(Child { moniker: target_child1.clone(), environment_name: None });
        fake.dynamic_children
            .push(Child { moniker: target_child2.clone(), environment_name: None });

        pretty_assertions::assert_eq! {
            hashmap! {
                ComponentRef::Self_ => hashset![
                    ComponentRef::Child(source_child1.clone()),
                    ComponentRef::Child(source_child2.clone()),
                    ComponentRef::Child(target_child1.clone()),
                    ComponentRef::Child(target_child2.clone()),
                ],
                ComponentRef::Child(source_child1) => hashset! [
                    ComponentRef::Child(target_child1.clone()),
                    ComponentRef::Child(target_child2.clone()),
                ],
                ComponentRef::Child(source_child2) => hashset! [
                    ComponentRef::Child(target_child1.clone()),
                    ComponentRef::Child(target_child2.clone()),
                ],
                ComponentRef::Child(target_child1) => hashset![],
                ComponentRef::Child(target_child2) => hashset![],
            },
            process_component_dependencies(&fake)
        };
    }

    #[fuchsia::test]
    fn test_source_does_not_exist() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        // This declaration is invalid because the offer target doesn't exist
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::static_child("childB".to_string()),
                source_name: "childBOffer".into(),
                target_name: "serviceSibling".into(),
                target: OfferTarget::static_child("childA".to_string()),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            children: vec![child_a.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA")],
                child("childA") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        );
    }

    #[fuchsia::test]
    fn test_use_from_child() {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Self_,
                source_name: "serviceSelf".into(),
                target_name: "serviceSelf".into(),
                target: OfferTarget::static_child("childA".to_string()),
                dependency_type: DependencyType::Weak,
                availability: Availability::Required,
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fdecl::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            uses: vec![UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Child("childA".to_string()),
                source_name: "test.protocol".into(),
                target_path: CapabilityPath::try_from("/svc/test.protocol").unwrap(),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![],
                child("childA") => hashset![ComponentRef::Self_],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_use_from_some_children() {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Self_,
                source_name: "serviceSelf".into(),
                target_name: "serviceSelf".into(),
                target: OfferTarget::static_child("childA".to_string()),
                dependency_type: DependencyType::Weak,
                availability: Availability::Required,
            })],
            children: vec![
                ChildDecl {
                    name: "childA".to_string(),
                    url: "ignored:///child".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                },
                ChildDecl {
                    name: "childB".to_string(),
                    url: "ignored:///child".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                },
            ],
            uses: vec![UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Child("childA".to_string()),
                source_name: "test.protocol".into(),
                target_path: CapabilityPath::try_from("/svc/test.protocol").unwrap(),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                // childB is a dependent because we consider all children
                // dependent, unless the component uses something from the
                // child.
                ComponentRef::Self_ => hashset![child("childB")],
                child("childA") => hashset![ComponentRef::Self_],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        );
    }

    #[fuchsia::test]
    fn test_use_from_child_offer_storage() {
        let decl = ComponentDecl {
            capabilities: vec![
                CapabilityDecl::Storage(StorageDecl {
                    name: "cdata".into(),
                    source: StorageDirectorySource::Child("childB".to_string()),
                    backing_dir: "directory".into(),
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                }),
                CapabilityDecl::Storage(StorageDecl {
                    name: "pdata".into(),
                    source: StorageDirectorySource::Parent,
                    backing_dir: "directory".into(),
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                }),
            ],
            offers: vec![
                OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    source_name: "cdata".into(),
                    target_name: "cdata".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    availability: Availability::Required,
                }),
                OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    source_name: "pdata".into(),
                    target_name: "pdata".into(),
                    target: OfferTarget::static_child("childA".to_string()),
                    availability: Availability::Required,
                }),
            ],
            children: vec![
                ChildDecl {
                    name: "childA".to_string(),
                    url: "ignored:///child".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                },
                ChildDecl {
                    name: "childB".to_string(),
                    url: "ignored:///child".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                },
            ],
            uses: vec![UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Child("childA".to_string()),
                source_name: "test.protocol".into(),
                target_path: CapabilityPath::try_from("/svc/test.protocol").unwrap(),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![],
                child("childA") => hashset![ComponentRef::Self_],
                child("childB") => hashset![child("childA")],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_use_from_child_weak() {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Self_,
                source_name: "serviceSelf".into(),
                target_name: "serviceSelf".into(),
                target: OfferTarget::static_child("childA".to_string()),
                dependency_type: DependencyType::Strong,
                availability: Availability::Required,
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fdecl::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            uses: vec![UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Child("childA".to_string()),
                source_name: "test.protocol".into(),
                target_path: CapabilityPath::try_from("/svc/test.protocol").unwrap(),
                dependency_type: DependencyType::Weak,
                availability: Availability::Required,
            })],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA")],
                child("childA") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_use_from_some_children_weak() {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Self_,
                source_name: "serviceSelf".into(),
                target_name: "serviceSelf".into(),
                target: OfferTarget::static_child("childA".to_string()),
                dependency_type: DependencyType::Weak,
                availability: Availability::Required,
            })],
            children: vec![
                ChildDecl {
                    name: "childA".to_string(),
                    url: "ignored:///child".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                },
                ChildDecl {
                    name: "childB".to_string(),
                    url: "ignored:///child".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                },
            ],
            uses: vec![
                UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Child("childA".to_string()),
                    source_name: "test.protocol".into(),
                    target_path: CapabilityPath::try_from("/svc/test.protocol").unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }),
                UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Child("childB".to_string()),
                    source_name: "test.protocol2".into(),
                    target_path: CapabilityPath::try_from("/svc/test.protocol2").unwrap(),
                    dependency_type: DependencyType::Weak,
                    availability: Availability::Required,
                }),
            ],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                // childB is a dependent because its use-from-child has a 'weak' dependency.
                ComponentRef::Self_ => hashset![child("childB")],
                child("childA") => hashset![ComponentRef::Self_],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    fn test_resolver_capability_creates_dependency() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        };
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Resolver(OfferResolverDecl {
                source: OfferSource::static_child("childA".to_string()),
                source_name: CapabilityName::try_from("resolver").unwrap(),
                target_name: CapabilityName::try_from("resolver").unwrap(),
                target: OfferTarget::static_child("childB".to_string()),
            })],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        pretty_assertions::assert_eq!(
            hashmap! {
                ComponentRef::Self_ => hashset![child("childA"), child("childB")],
                child("childA") => hashset![child("childB")],
                child("childB") => hashset![],
            },
            process_component_dependencies(&FakeComponent::from_decl(decl))
        )
    }

    #[fuchsia::test]
    async fn action_shutdown_blocks_stop() {
        let test = ActionsTest::new("root", vec![], None).await;
        let component = test.model.root().clone();
        let mut action_set = component.lock_actions().await;

        // Register some actions, and get notifications. Use `register_inner` so we can register
        // the action without immediately running it.
        let (task1, nf1) = action_set.register_inner(&component, ShutdownAction::new());
        let (task2, nf2) = action_set.register_inner(&component, StopAction::new(false, false));

        drop(action_set);

        // Complete actions, while checking futures.
        ActionSet::finish(&component, &ActionKey::Shutdown).await;

        // nf2 should be blocked on task1 completing.
        assert!(nf1.fut.peek().is_none());
        assert!(nf2.fut.peek().is_none());
        task1.unwrap().tx.send(Ok(())).unwrap();
        task2.unwrap().spawn();
        nf1.await.unwrap();
        nf2.await.unwrap();
    }

    #[fuchsia::test]
    async fn action_shutdown_stop_stop() {
        let test = ActionsTest::new("root", vec![], None).await;
        let component = test.model.root().clone();
        let mut action_set = component.lock_actions().await;

        // Register some actions, and get notifications. Use `register_inner` so we can register
        // the action without immediately running it.
        let (task1, nf1) = action_set.register_inner(&component, ShutdownAction::new());
        let (task2, nf2) = action_set.register_inner(&component, StopAction::new(false, false));
        let (task3, nf3) = action_set.register_inner(&component, StopAction::new(false, false));

        drop(action_set);

        // Complete actions, while checking notifications.
        ActionSet::finish(&component, &ActionKey::Shutdown).await;

        // nf2 and nf3 should be blocked on task1 completing.
        assert!(nf1.fut.peek().is_none());
        assert!(nf2.fut.peek().is_none());
        task1.unwrap().tx.send(Ok(())).unwrap();
        task2.unwrap().spawn();
        assert!(task3.is_none());
        nf1.await.unwrap();
        nf2.await.unwrap();
        nf3.await.unwrap();
    }

    #[fuchsia::test]
    async fn shutdown_one_component() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Start the component. This should cause the component to have an `Execution`.
        let component = test.look_up(vec!["a"].into()).await;
        test.model
            .start_instance(&component.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component).await);
        let a_info = ComponentInfo::new(component.clone()).await;

        // Register shutdown action, and wait for it. Component should shut down (no more
        // `Execution`).
        ActionSet::register(a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        a_info.check_is_shut_down(&test.runner).await;

        // Trying to start the component should fail because it's shut down.
        test.model
            .start_instance(&a_info.component.abs_moniker, &StartReason::Eager)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Shut down the component again. This succeeds, but has no additional effect.
        ActionSet::register(a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        a_info.check_is_shut_down(&test.runner).await;
    }

    #[fuchsia::test]
    async fn shutdown_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("container").build()),
            (
                "container",
                ComponentDeclBuilder::new()
                    .add_transient_collection("coll")
                    .add_lazy_child("c")
                    .build(),
            ),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Start the components. This should cause them to have an `Execution`.
        let component_container = test.look_up(vec!["container"].into()).await;
        let component_a = test.look_up(vec!["container", "coll:a"].into()).await;
        let component_b = test.look_up(vec!["container", "coll:b"].into()).await;
        let component_c = test.look_up(vec!["container", "c"].into()).await;
        test.model
            .start_instance(&component_container.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start container");
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:a");
        test.model
            .start_instance(&component_b.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:b");
        test.model
            .start_instance(&component_c.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start c");
        assert!(is_executing(&component_container).await);
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(has_child(&component_container, "coll:a").await);
        assert!(has_child(&component_container, "coll:b").await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_container_info = ComponentInfo::new(component_container).await;

        // Register shutdown action, and wait for it. Components should shut down (no more
        // `Execution`). Also, the instances in the collection should have been destroyed because
        // they were transient.
        ActionSet::register(component_container_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_container_info.check_is_shut_down(&test.runner).await;
        assert!(!has_child(&component_container_info.component, "coll:a").await);
        assert!(!has_child(&component_container_info.component, "coll:b").await);
        assert!(has_child(&component_container_info.component, "c").await);
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;

        // Verify events.
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut next: Vec<_> = events.drain(0..3).collect();
            next.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["container", "c"].into()),
                Lifecycle::Stop(vec!["container", "coll:a"].into()),
                Lifecycle::Stop(vec!["container", "coll:b"].into()),
            ];
            assert_eq!(next, expected);

            // These components were destroyed because they lived in a transient collection.
            let mut next: Vec<_> = events.drain(0..2).collect();
            next.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Destroy(vec!["container", "coll:a"].into()),
                Lifecycle::Destroy(vec!["container", "coll:b"].into()),
            ];
            assert_eq!(next, expected);
        }
    }

    #[fuchsia::test]
    async fn shutdown_dynamic_offers() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("container").build()),
            (
                "container",
                ComponentDeclBuilder::new()
                    .add_collection(
                        CollectionDeclBuilder::new_transient_collection("coll")
                            .allowed_offers(AllowedOffers::StaticAndDynamic)
                            .build(),
                    )
                    .add_lazy_child("c")
                    .offer(cm_rust::OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child(ChildRef {
                            name: "c".to_string(),
                            collection: None,
                        }),
                        source_name: "static_offer_source".into(),
                        target: OfferTarget::Collection("coll".to_string()),
                        target_name: "static_offer_target".into(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child_with_args(
            "coll",
            "b",
            fcomponent::CreateChildArgs {
                dynamic_offers: Some(vec![fdecl::Offer::Protocol(fdecl::OfferProtocol {
                    source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                        name: "a".to_string(),
                        collection: Some("coll".to_string()),
                    })),
                    source_name: Some("dyn_offer_source_name".to_string()),
                    target_name: Some("dyn_offer_target_name".to_string()),
                    dependency_type: Some(fdecl::DependencyType::Strong),
                    ..fdecl::OfferProtocol::EMPTY
                })]),
                ..fcomponent::CreateChildArgs::EMPTY
            },
        )
        .await;

        // Start the components. This should cause them to have an `Execution`.
        let component_container = test.look_up(vec!["container"].into()).await;
        let component_a = test.look_up(vec!["container", "coll:a"].into()).await;
        let component_b = test.look_up(vec!["container", "coll:b"].into()).await;
        let component_c = test.look_up(vec!["container", "c"].into()).await;
        test.model
            .start_instance(&component_container.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start container");
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:a");
        test.model
            .start_instance(&component_b.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:b");
        test.model
            .start_instance(&component_c.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start c");
        assert!(is_executing(&component_container).await);
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(has_child(&component_container, "coll:a").await);
        assert!(has_child(&component_container, "coll:b").await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_container_info = ComponentInfo::new(component_container).await;

        // Register shutdown action, and wait for it. Components should shut down (no more
        // `Execution`). Also, the instances in the collection should have been destroyed because
        // they were transient.
        ActionSet::register(component_container_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_container_info.check_is_shut_down(&test.runner).await;
        assert!(!has_child(&component_container_info.component, "coll:a").await);
        assert!(!has_child(&component_container_info.component, "coll:b").await);
        assert!(has_child(&component_container_info.component, "c").await);
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;

        // Verify events.
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();

            pretty_assertions::assert_eq!(
                vec![
                    Lifecycle::Stop(vec!["container", "coll:b"].into()),
                    Lifecycle::Stop(vec!["container", "coll:a"].into()),
                    Lifecycle::Stop(vec!["container", "c"].into()),
                ],
                events.drain(0..3).collect::<Vec<_>>()
            );

            // The order here is nondeterministic.
            pretty_assertions::assert_eq!(
                btreeset![
                    Lifecycle::Destroy(vec!["container", "coll:b"].into()),
                    Lifecycle::Destroy(vec!["container", "coll:a"].into()),
                ],
                events.drain(0..2).collect::<BTreeSet<_>>()
            );
            pretty_assertions::assert_eq!(vec![Lifecycle::Stop(vec!["container"].into()),], events);
        }
    }

    #[fuchsia::test]
    async fn shutdown_not_started() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        assert!(!is_executing(&component_a).await);
        assert!(!is_executing(&component_b).await);

        // Register shutdown action on "a", and wait for it.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a).await);
        assert!(execution_is_shut_down(&component_b).await);

        // Now "a" is shut down. There should be no events though because the component was
        // never started.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a).await);
        assert!(execution_is_shut_down(&component_b).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, Vec::<Lifecycle>::new());
        }
    }

    #[fuchsia::test]
    async fn shutdown_not_resolved() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);

        // Register shutdown action on "a", and wait for it.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a).await);
        // Get component without resolving it.
        let component_b = {
            let state = component_a.lock_state().await;
            match *state {
                InstanceState::Resolved(ref s) => {
                    s.get_child(&"b".into()).expect("child b not found").clone()
                }
                _ => panic!("not resolved"),
            }
        };
        assert!(execution_is_shut_down(&component_b).await);
        assert!(is_unresolved(&component_b).await);

        // Now "a" is shut down. There should be no event for "b" because it was never started
        // (or resolved).
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::Stop(vec!["a"].into())]);
        }
    }

    /// Shut down `a`:
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c   d
    #[fuchsia::test]
    async fn shutdown_hierarchy() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "b", "c"].into()),
                Lifecycle::Stop(vec!["a", "b", "d"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(
                events,
                vec![Lifecycle::Stop(vec!["a", "b"].into()), Lifecycle::Stop(vec!["a"].into())]
            );
        }
    }

    /// Shut down `a`:
    ///   a
    ///    \
    ///     b
    ///   / | \
    ///  c<-d->e
    /// In this case C and E use a service provided by d
    #[fuchsia::test]
    async fn shutdown_with_multiple_deps() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::static_child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::static_child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceD".into(),
                        source_path: Some("/svc/serviceD".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;
        let component_e = test.look_up(vec!["a", "b", "e"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_e).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;
        let component_e_info = ComponentInfo::new(component_e).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        component_e_info.check_is_shut_down(&test.runner).await;

        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let mut expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "b", "c"].into()),
                Lifecycle::Stop(vec!["a", "b", "e"].into()),
            ];
            assert_eq!(first, expected);

            let next: Vec<_> = events.drain(0..1).collect();
            expected = vec![Lifecycle::Stop(vec!["a", "b", "d"].into())];
            assert_eq!(next, expected);

            assert_eq!(
                events,
                vec![Lifecycle::Stop(vec!["a", "b"].into()), Lifecycle::Stop(vec!["a"].into())]
            );
        }
    }

    /// Shut down `a`:
    ///    a
    ///     \
    ///      b
    ///   / / \  \
    ///  c<-d->e->f
    /// In this case C and E use a service provided by D and
    /// F uses a service provided by E, shutdown order should be
    /// {F}, {C, E}, {D}, {B}, {A}
    /// Note that C must stop before D, but may stop before or after
    /// either of F and E.
    #[fuchsia::test]
    async fn shutdown_with_multiple_out_and_longer_chain() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .add_eager_child("f")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::static_child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::static_child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("e".to_string()),
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: OfferTarget::static_child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceD".into(),
                        source_path: Some("/svc/serviceD".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceE".into(),
                        source_path: Some("/svc/serviceE".parse().unwrap()),
                    })
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceE".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let moniker_a: AbsoluteMoniker = vec!["a"].into();
        let moniker_b: AbsoluteMoniker = vec!["a", "b"].into();
        let moniker_c: AbsoluteMoniker = vec!["a", "b", "c"].into();
        let moniker_d: AbsoluteMoniker = vec!["a", "b", "d"].into();
        let moniker_e: AbsoluteMoniker = vec!["a", "b", "e"].into();
        let moniker_f: AbsoluteMoniker = vec!["a", "b", "f"].into();
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(moniker_a.clone()).await;
        let component_b = test.look_up(moniker_b.clone()).await;
        let component_c = test.look_up(moniker_c.clone()).await;
        let component_d = test.look_up(moniker_d.clone()).await;
        let component_e = test.look_up(moniker_e.clone()).await;
        let component_f = test.look_up(moniker_f.clone()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_e).await);
        assert!(is_executing(&component_f).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;
        let component_e_info = ComponentInfo::new(component_e).await;
        let component_f_info = ComponentInfo::new(component_f).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        component_e_info.check_is_shut_down(&test.runner).await;
        component_f_info.check_is_shut_down(&test.runner).await;

        let mut comes_after: HashMap<AbsoluteMoniker, Vec<AbsoluteMoniker>> = HashMap::new();
        comes_after.insert(moniker_a.clone(), vec![moniker_b.clone()]);
        // technically we could just depend on 'D' since it is the last of b's
        // children, but we add all the children for resilence against the
        // future
        comes_after.insert(
            moniker_b.clone(),
            vec![moniker_c.clone(), moniker_d.clone(), moniker_e.clone(), moniker_f.clone()],
        );
        comes_after.insert(moniker_d.clone(), vec![moniker_c.clone(), moniker_e.clone()]);
        comes_after.insert(moniker_c.clone(), vec![]);
        comes_after.insert(moniker_e.clone(), vec![moniker_f.clone()]);
        comes_after.insert(moniker_f.clone(), vec![]);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();

            for e in events {
                match e {
                    Lifecycle::Stop(moniker) => match comes_after.remove(&moniker) {
                        Some(dependents) => {
                            for d in dependents {
                                if comes_after.contains_key(&d) {
                                    panic!("{} stopped before its dependent {}", moniker, d);
                                }
                            }
                        }
                        None => {
                            panic!("{} was unknown or shut down more than once", moniker);
                        }
                    },
                    _ => {
                        panic!("Unexpected lifecycle type");
                    }
                }
            }
        }
    }

    /// Shut down `a`:
    ///           a
    ///
    ///           |
    ///
    ///     +---- b ----+
    ///    /             \
    ///   /     /   \     \
    ///
    ///  c <~~ d ~~> e ~~> f
    ///          \       /
    ///           +~~>~~+
    /// In this case C and E use a service provided by D and
    /// F uses a services provided by E and D, shutdown order should be F must
    /// stop before E and {C,E,F} must stop before D. C may stop before or
    /// after either of {F, E}.
    #[fuchsia::test]
    async fn shutdown_with_multiple_out_multiple_in() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .add_eager_child("f")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::static_child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::static_child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::static_child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("e".to_string()),
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: OfferTarget::static_child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceD".into(),
                        source_path: Some("/svc/serviceD".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceE".into(),
                        source_path: Some("/svc/serviceE".parse().unwrap()),
                    })
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceE".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceE".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let moniker_a: AbsoluteMoniker = vec!["a"].into();
        let moniker_b: AbsoluteMoniker = vec!["a", "b"].into();
        let moniker_c: AbsoluteMoniker = vec!["a", "b", "c"].into();
        let moniker_d: AbsoluteMoniker = vec!["a", "b", "d"].into();
        let moniker_e: AbsoluteMoniker = vec!["a", "b", "e"].into();
        let moniker_f: AbsoluteMoniker = vec!["a", "b", "f"].into();
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(moniker_a.clone()).await;
        let component_b = test.look_up(moniker_b.clone()).await;
        let component_c = test.look_up(moniker_c.clone()).await;
        let component_d = test.look_up(moniker_d.clone()).await;
        let component_e = test.look_up(moniker_e.clone()).await;
        let component_f = test.look_up(moniker_f.clone()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_e).await);
        assert!(is_executing(&component_f).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;
        let component_e_info = ComponentInfo::new(component_e).await;
        let component_f_info = ComponentInfo::new(component_f).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        component_e_info.check_is_shut_down(&test.runner).await;
        component_f_info.check_is_shut_down(&test.runner).await;

        let mut comes_after: HashMap<AbsoluteMoniker, Vec<AbsoluteMoniker>> = HashMap::new();
        comes_after.insert(moniker_a.clone(), vec![moniker_b.clone()]);
        // technically we could just depend on 'D' since it is the last of b's
        // children, but we add all the children for resilence against the
        // future
        comes_after.insert(
            moniker_b.clone(),
            vec![moniker_c.clone(), moniker_d.clone(), moniker_e.clone(), moniker_f.clone()],
        );
        comes_after.insert(
            moniker_d.clone(),
            vec![moniker_c.clone(), moniker_e.clone(), moniker_f.clone()],
        );
        comes_after.insert(moniker_c.clone(), vec![]);
        comes_after.insert(moniker_e.clone(), vec![moniker_f.clone()]);
        comes_after.insert(moniker_f.clone(), vec![]);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();

            for e in events {
                match e {
                    Lifecycle::Stop(moniker) => {
                        let dependents = comes_after.remove(&moniker).expect(&format!(
                            "{} was unknown or shut down more than once",
                            moniker
                        ));
                        for d in dependents {
                            if comes_after.contains_key(&d) {
                                panic!("{} stopped before its dependent {}", moniker, d);
                            }
                        }
                    }
                    _ => {
                        panic!("Unexpected lifecycle type");
                    }
                }
            }
        }
    }

    /// Shut down `a`:
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c-->d
    /// In this case D uses a resource exposed by C
    #[fuchsia::test]
    async fn shutdown_with_dependency() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("c".to_string()),
                        source_name: "serviceC".into(),
                        target_name: "serviceC".into(),
                        target: OfferTarget::static_child("d".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceC".into(),
                        source_path: Some("/svc/serviceC".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceC".into(),
                        target_name: "serviceC".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceC".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;

        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "b", "d"].into()),
                Lifecycle::Stop(vec!["a", "b", "c"].into()),
                Lifecycle::Stop(vec!["a", "b"].into()),
                Lifecycle::Stop(vec!["a"].into()),
            ];
            assert_eq!(events, expected);
        }
    }

    /// Shut down `a`:
    ///   a     (a use b)
    ///  / \
    /// b    c
    /// In this case, c shuts down first, then a, then b.
    #[fuchsia::test]
    async fn shutdown_use_from_child() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .add_eager_child("c")
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Child("b".to_string()),
                        source_name: "serviceC".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceC".into(),
                        source_path: Some("/svc/serviceC".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceC".into(),
                        target_name: "serviceC".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            ("c", ComponentDeclBuilder::new().build()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "c"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;

        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "c"].into()),
                Lifecycle::Stop(vec!["a"].into()),
                Lifecycle::Stop(vec!["a", "b"].into()),
            ];
            assert_eq!(events, expected);
        }
    }

    /// Shut down `a`:
    ///   a     (a use b, and b use c)
    ///  / \
    /// b    c
    /// In this case, a shuts down first, then b, then c.
    #[fuchsia::test]
    async fn shutdown_use_from_child_that_uses_from_sibling() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .add_eager_child("c")
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Child("b".to_string()),
                        source_name: "serviceB".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceB").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("c".to_string()),
                        source_name: "serviceC".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        target_name: "serviceB".into(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceB".into(),
                        source_path: Some("/svc/serviceB".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceB".into(),
                        target_name: "serviceB".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceC".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceC".into(),
                        source_path: Some("/svc/serviceC".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceC".into(),
                        target_name: "serviceC".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "c"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;

        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a"].into()),
                Lifecycle::Stop(vec!["a", "b"].into()),
                Lifecycle::Stop(vec!["a", "c"].into()),
            ];
            assert_eq!(events, expected);
        }
    }

    /// Shut down `a`:
    ///   a     (a use b weak)
    ///  / \
    /// b    c
    /// In this case, b or c shutdown first (arbitrary order), then a.
    #[fuchsia::test]
    async fn shutdown_use_from_child_weak() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .add_eager_child("c")
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Child("b".to_string()),
                        source_name: "serviceC".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        dependency_type: DependencyType::Weak,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceC".into(),
                        source_path: Some("/svc/serviceC".parse().unwrap()),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceC".into(),
                        target_name: "serviceC".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            ("c", ComponentDeclBuilder::new().build()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "c"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;

        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let expected1: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "c"].into()),
                Lifecycle::Stop(vec!["a", "b"].into()),
                Lifecycle::Stop(vec!["a"].into()),
            ];
            let expected2: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "b"].into()),
                Lifecycle::Stop(vec!["a", "c"].into()),
                Lifecycle::Stop(vec!["a"].into()),
            ];
            assert!(events == expected1 || events == expected2);
        }
    }

    /// Shut down `b`:
    ///  a
    ///   \
    ///    b
    ///     \
    ///      b
    ///       \
    ///      ...
    ///
    /// `b` is a child of itself, but shutdown should still be able to complete.
    #[fuchsia::test]
    async fn shutdown_self_referential() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_b2 = test.look_up(vec!["a", "b", "b"].into()).await;

        // Start second `b`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start b2");
        test.model
            .start_instance(&component_b.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start b2");
        test.model
            .start_instance(&component_b2.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start b2");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_b2).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_b2_info = ComponentInfo::new(component_b2).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_b2_info.check_is_shut_down(&test.runner).await;
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a", "b", "b"].into()),
                    Lifecycle::Stop(vec!["a", "b"].into()),
                    Lifecycle::Stop(vec!["a"].into())
                ]
            );
        }
    }

    /// Shut down `a`:
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c   d
    ///
    /// `b` fails to finish shutdown the first time, but succeeds the second time.
    #[fuchsia::test]
    async fn shutdown_error() {
        struct StopErrorHook {
            moniker: AbsoluteMoniker,
        }

        impl StopErrorHook {
            fn new(moniker: AbsoluteMoniker) -> Self {
                Self { moniker }
            }

            fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
                vec![HooksRegistration::new(
                    "StopErrorHook",
                    vec![EventType::Stopped],
                    Arc::downgrade(self) as Weak<dyn Hook>,
                )]
            }

            async fn on_shutdown_instance_async(
                &self,
                target_moniker: &AbsoluteMoniker,
            ) -> Result<(), ModelError> {
                if *target_moniker == self.moniker {
                    return Err(ModelError::unsupported("ouch"));
                }
                Ok(())
            }
        }

        #[async_trait]
        impl Hook for StopErrorHook {
            async fn on(self: Arc<Self>, event: &hooks::Event) -> Result<(), ModelError> {
                let target_moniker = event
                    .target_moniker
                    .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
                if let Ok(EventPayload::Stopped { .. }) = event.result {
                    self.on_shutdown_instance_async(target_moniker).await?;
                }
                Ok(())
            }
        }

        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let error_hook = Arc::new(StopErrorHook::new(vec!["a", "b"].into()));
        let test = ActionsTest::new_with_hooks("root", components, None, error_hook.hooks()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;

        // Register shutdown action on "a", and wait for it. "b"'s component shuts down, but "b"
        // returns an error so "a" does not.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect_err("shutdown succeeded unexpectedly");
        component_a_info.check_not_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "b", "c"].into()),
                Lifecycle::Stop(vec!["a", "b", "d"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(events, vec![Lifecycle::Stop(vec!["a", "b"].into())],);
        }

        // Register shutdown action on "a" again. "b"'s shutdown succeeds (it's a no-op), and
        // "a" is allowed to shut down this time.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "b", "c"].into()),
                Lifecycle::Stop(vec!["a", "b", "d"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(
                events,
                vec![Lifecycle::Stop(vec!["a", "b"].into()), Lifecycle::Stop(vec!["a"].into())]
            );
        }
    }
}
