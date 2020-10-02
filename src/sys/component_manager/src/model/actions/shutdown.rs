// Copyright 2019 The Fuchsia Authors. All right reserved.
// Use of this source code is goverend by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionSet},
        error::ModelError,
        moniker::{ChildMoniker, PartialMoniker},
        realm::{Realm, RealmState},
    },
    cm_rust::{
        CapabilityDecl, ComponentDecl, DependencyType, OfferDecl, OfferDirectorySource,
        OfferResolverSource, OfferRunnerSource, OfferServiceSource, OfferStorageSource,
        OfferTarget, RegistrationSource, StorageDirectorySource,
    },
    futures::future::select_all,
    maplit::hashset,
    std::collections::{HashMap, HashSet},
    std::fmt,
    std::sync::Arc,
};

/// A DependencyNode represents a provider or user of a capability. This
/// may be either a component or a component collection.
#[derive(Debug, PartialEq, Eq, Hash, Clone, PartialOrd, Ord)]
pub enum DependencyNode {
    Child(String),
    Collection(String),
}

/// Examines a group of StorageDecls looking for one whose name matches the
/// String passed in and whose source is a child. `None` is returned if either
/// no declaration has the specified name or the declaration represents an
/// offer from Self or Parent.
fn find_storage_provider(capabilities: &Vec<CapabilityDecl>, name: &str) -> Option<String> {
    for decl in capabilities {
        match decl {
            CapabilityDecl::Storage(decl) if decl.name == name => match &decl.source {
                StorageDirectorySource::Child(child) => {
                    return Some(child.to_string());
                }
                StorageDirectorySource::Self_ | StorageDirectorySource::Parent => {
                    return None;
                }
            },
            _ => {}
        }
    }
    None
}

async fn shutdown_component(child: ShutdownInfo) -> Result<ChildMoniker, ModelError> {
    ActionSet::register(child.realm, Action::Shutdown).await.await?;
    Ok(child.moniker.clone())
}

/// Structure which holds bidirectional capability maps used during the
/// shutdown process.
struct ShutdownJob {
    /// A map from users of capabilities to the components that provide those
    /// capabilities
    target_to_sources: HashMap<ChildMoniker, Vec<ChildMoniker>>,
    /// A map from providers of capabilities to those components which use the
    /// capabilities
    source_to_targets: HashMap<ChildMoniker, ShutdownInfo>,
}

/// ShutdownJob encapsulates the logic and state require to shutdown a realm.
impl ShutdownJob {
    /// Creates a new ShutdownJob by examining the Realm's declaration and
    /// runtime state to build up the necessary data structures to stop
    /// components in the realm in dependency order.
    pub async fn new(state: &RealmState) -> ShutdownJob {
        // `children` represents the dependency relationships between the
        // children as expressed in the realm's component declaration.
        // This representation must be reconciled with the runtime state of the
        // realm. This means mapping children in the declaration with the one
        // or more children that may exist in collections and one or more
        // instances with a matching PartialMoniker that may exist.
        let children = process_component_dependencies(state.decl());
        let mut source_to_targets: HashMap<ChildMoniker, ShutdownInfo> = HashMap::new();

        for (child_name, sibling_deps) in children {
            let deps = get_child_monikers(&sibling_deps, state);

            let singleton_child_set = hashset![child_name];
            // The shutdown target may be a collection, if so this will expand
            // the collection out into a list of all its members, otherwise it
            // contains a single component.
            let matching_children: Vec<_> =
                get_child_monikers(&singleton_child_set, state).into_iter().collect();
            for child in matching_children {
                let realm = state
                    .get_child_instance(&child)
                    .expect("component not found in children")
                    .clone();

                source_to_targets.insert(
                    child.clone(),
                    ShutdownInfo { moniker: child, dependents: deps.clone(), realm: realm },
                );
            }
        }

        let mut target_to_sources: HashMap<ChildMoniker, Vec<ChildMoniker>> = HashMap::new();
        // Look at each of the children
        for provider in source_to_targets.values() {
            // All listed siblings are ones that depend on this child
            // and all those siblings must stop before this one
            for consumer in &provider.dependents {
                // Make or update a map entry for the consumer that points to the
                // list of siblings that offer it capabilities
                target_to_sources
                    .entry(consumer.clone())
                    .or_insert(vec![])
                    .push(provider.moniker.clone());
            }
        }
        let new_job = ShutdownJob { source_to_targets, target_to_sources };
        return new_job;
    }

    /// Perform shutdown of the Realm that was used to create this ShutdownJob
    /// A Realm must wait to shut down until all its children are shut down.
    /// The shutdown procedure looks at the children of Realm, if any, and
    /// determines the dependency relationships of the children.
    pub async fn execute(&mut self) -> Result<(), ModelError> {
        // Relationship maps are maintained to track dependencies. A map is
        // maintained both from a Realm to its dependents and from a Realm to
        // that Realm's dependencies. With this dependency tracking, the
        // children of the Realm can be shut down progressively in dependency
        // order.
        //
        // The progressive shutdown of Realms is performed in this order:
        // Note: These steps continue until the shutdown process is no longer
        // asynchronously waiting for any shut downs to complete.
        //   * Identify the one or more Realms that have no dependents
        //   * A shutdown action is set to the identified realms. During the
        //     shut down process, the result of the process is received
        //     asynchronously.
        //   * After a Realm is shut down, the Realms are removed from the list
        //     of dependents of the Realms on which they had a dependency.
        //   * The list of Realms is checked again to see which Realms have no
        //     remaining dependents.

        // Look for any children that have no dependents
        let mut stop_targets = vec![];

        for moniker in self.source_to_targets.keys().map(|key| key.clone()).collect::<Vec<_>>() {
            let no_dependents = {
                let info = self.source_to_targets.get(&moniker).expect("key disappeared from map");
                info.dependents.is_empty()
            };
            if no_dependents {
                stop_targets.push(
                    self.source_to_targets.remove(&moniker).expect("key disappeared from map"),
                );
            }
        }

        let mut futs = vec![];
        // Continue while we have new stop targets or unfinished futures
        while !stop_targets.is_empty() || !futs.is_empty() {
            for target in stop_targets.drain(..) {
                futs.push(Box::pin(shutdown_component(target)));
            }

            let (moniker, _, remaining) = select_all(futs).await;
            futs = remaining;

            let moniker = moniker?;

            // Look up the dependencies of the component that stopped
            match self.target_to_sources.remove(&moniker) {
                Some(vec) => {
                    for dep_moniker in vec {
                        let ready_to_stop = {
                            if let Some(child) = self.source_to_targets.get_mut(&dep_moniker) {
                                child.dependents.remove(&moniker);
                                // Have all of this components dependents stopped?
                                child.dependents.is_empty()
                            } else {
                                // The component that provided a capability to
                                // the stopped component doesn't exist or
                                // somehow already stopped. This is unexpected.
                                panic!(
                                    "The component '{}' appears to have stopped before its \
                                     dependency '{}'",
                                    moniker, dep_moniker
                                );
                            }
                        };

                        // This components had zero remaining dependents
                        if ready_to_stop {
                            stop_targets.push(
                                self.source_to_targets
                                    .remove(&dep_moniker)
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

pub async fn do_shutdown(realm: Arc<Realm>) -> Result<(), ModelError> {
    {
        let state_lock = realm.lock_state().await;
        {
            let exec_state = realm.lock_execution().await;
            if exec_state.is_shut_down() {
                return Ok(());
            }
        }
        if let Some(state) = state_lock.as_ref() {
            let mut shutdown_job = ShutdownJob::new(state).await;
            drop(state_lock);
            Box::pin(shutdown_job.execute()).await?;
        }
    }
    // Now that all children have shut down, shut down the parent.
    // TODO: Put the parent in a "shutting down" state so that if it creates new instances
    // after this point, they are created in a shut down state.
    realm.stop_instance(true).await?;

    Ok(())
}

/// Used to track information during the shutdown process. The dependents
/// are all the component which must stop before the component represented
/// by this struct.
struct ShutdownInfo {
    // TODO(jmatt) reduce visibility of fields
    /// The identifier for this component
    pub moniker: ChildMoniker,
    /// The components that this component offers capabilities to
    pub dependents: HashSet<ChildMoniker>,
    pub realm: Arc<Realm>,
}

impl fmt::Debug for ShutdownInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "moniker: '{:?}'", self.moniker)
    }
}

/// Given a set of DependencyNodes, find all the ChildMonikers in the supplied
/// Realm that match.
fn get_child_monikers(
    child_names: &HashSet<DependencyNode>,
    realm_state: &RealmState,
) -> HashSet<ChildMoniker> {
    let mut deps: HashSet<ChildMoniker> = HashSet::new();
    let realms = realm_state.all_child_realms();

    for child in child_names {
        match child {
            DependencyNode::Child(name) => {
                let dep_moniker = PartialMoniker::new(name.to_string(), None);
                let matching_children = realm_state.get_all_child_monikers(&dep_moniker);
                for m in matching_children {
                    deps.insert(m);
                }
            }
            DependencyNode::Collection(name) => {
                for moniker in realms.keys() {
                    match moniker.collection() {
                        Some(m) => {
                            if m == name {
                                deps.insert(moniker.clone());
                            }
                        }
                        None => {}
                    }
                }
            }
        }
    }
    deps
}

/// Maps a dependency node (child or collection) to the nodes that depend on it.
pub type DependencyMap = HashMap<DependencyNode, HashSet<DependencyNode>>;

/// For a given ComponentDecl, parse it, identify capability dependencies
/// between children and collections in the ComponentDecl. A map is returned
/// which maps from a child to a set of other children to which that child
/// provides capabilities. The siblings to which the child offers capabilities
/// must be shut down before that child. This function panics if there is a
/// capability routing where either the source or target is not present in this
/// ComponentDecl. Panics are not expected because ComponentDecls should be
/// validated before this function is called.
pub fn process_component_dependencies(decl: &ComponentDecl) -> DependencyMap {
    let mut dependency_map: DependencyMap = decl
        .children
        .iter()
        .map(|c| (DependencyNode::Child(c.name.clone()), HashSet::new()))
        .collect();
    dependency_map.extend(
        decl.collections
            .iter()
            .map(|c| (DependencyNode::Collection(c.name.clone()), HashSet::new())),
    );

    get_dependencies_from_offers(decl, &mut dependency_map);
    get_dependencies_from_environments(decl, &mut dependency_map);
    dependency_map
}

/// Loops through all the offer declarations to determine which siblings
/// provide capabilities to other siblings.
fn get_dependencies_from_offers(decl: &ComponentDecl, dependency_map: &mut DependencyMap) {
    for dep in &decl.offers {
        // Identify the source and target of the offer. We only care about
        // dependencies where the provider of the dependency is another child,
        // otherwise the capability comes from the parent or component manager
        // itself in which case the relationship is not relevant for ordering
        // here.
        let source_target_pairs = match dep {
            OfferDecl::Protocol(svc_offer) => {
                if svc_offer.dependency_type == DependencyType::WeakForMigration {
                    // weak dependencies are ignored by this algorithm, because weak dependencies
                    // can be broken arbitrarily.
                    continue;
                }
                match &svc_offer.source {
                    OfferServiceSource::Child(source) => match &svc_offer.target {
                        OfferTarget::Child(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Child(target.clone()),
                        )],
                        OfferTarget::Collection(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Collection(target.clone()),
                        )],
                    },
                    OfferServiceSource::Self_ | OfferServiceSource::Parent => {
                        // Capabilities offered by the parent or routed in from
                        // the realm are not relevant.
                        continue;
                    }
                }
            }
            OfferDecl::Service(svc_offers) => {
                let mut pairs = vec![];
                for svc_offer in &svc_offers.sources {
                    match &svc_offer.source {
                        OfferServiceSource::Child(source) => match &svc_offers.target {
                            OfferTarget::Child(target) => pairs.push((
                                DependencyNode::Child(source.clone()),
                                DependencyNode::Child(target.clone()),
                            )),
                            OfferTarget::Collection(target) => pairs.push((
                                DependencyNode::Child(source.clone()),
                                DependencyNode::Collection(target.clone()),
                            )),
                        },
                        OfferServiceSource::Self_ | OfferServiceSource::Parent => {
                            // Capabilities offered by the parent or routed in
                            // from the realm are not relevant.
                            continue;
                        }
                    }
                }
                pairs
            }
            OfferDecl::Directory(dir_offer) => {
                if dir_offer.dependency_type == DependencyType::WeakForMigration {
                    // weak dependencies are ignored by this algorithm, because weak dependencies
                    // can be broken arbitrarily.
                    continue;
                }
                match &dir_offer.source {
                    OfferDirectorySource::Child(source) => match &dir_offer.target {
                        OfferTarget::Child(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Child(target.clone()),
                        )],
                        OfferTarget::Collection(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Collection(target.clone()),
                        )],
                    },
                    OfferDirectorySource::Self_
                    | OfferDirectorySource::Parent
                    | OfferDirectorySource::Framework => {
                        // Capabilities offered by the parent or routed in from
                        // the realm are not relevant.
                        continue;
                    }
                }
            }
            OfferDecl::Storage(s) => {
                match &s.source {
                    OfferStorageSource::Self_ => {
                        match find_storage_provider(&decl.capabilities, s.source_name.str()) {
                            Some(storage_source) => match &s.target {
                                OfferTarget::Child(target) => vec![(
                                    DependencyNode::Child(storage_source.clone()),
                                    DependencyNode::Child(target.clone()),
                                )],
                                OfferTarget::Collection(target) => vec![(
                                    DependencyNode::Child(storage_source.clone()),
                                    DependencyNode::Collection(target.clone()),
                                )],
                            },
                            None => {
                                // The storage offer is not from a child, so it
                                // can be ignored.
                                continue;
                            }
                        }
                    }
                    OfferStorageSource::Parent => {
                        // Capabilities coming from the parent aren't tracked.
                        continue;
                    }
                }
            }
            OfferDecl::Runner(runner_offer) => {
                match &runner_offer.source {
                    OfferRunnerSource::Child(source) => match &runner_offer.target {
                        OfferTarget::Child(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Child(target.clone()),
                        )],
                        OfferTarget::Collection(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Collection(target.clone()),
                        )],
                    },
                    OfferRunnerSource::Self_ | OfferRunnerSource::Parent => {
                        // Capabilities coming from the parent aren't tracked.
                        continue;
                    }
                }
            }
            OfferDecl::Resolver(resolver_offer) => {
                match &resolver_offer.source {
                    OfferResolverSource::Child(source) => match &resolver_offer.target {
                        OfferTarget::Child(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Child(target.clone()),
                        )],
                        OfferTarget::Collection(target) => vec![(
                            DependencyNode::Child(source.clone()),
                            DependencyNode::Collection(target.clone()),
                        )],
                    },
                    OfferResolverSource::Self_ | OfferResolverSource::Parent => {
                        // Capabilities coming from the parent aren't tracked.
                        continue;
                    }
                }
            }
            OfferDecl::Event(_) => {
                // Events aren't tracked as dependencies for shutdown.
                continue;
            }
        };

        for (capability_provider, capability_target) in source_target_pairs {
            if !dependency_map.contains_key(&capability_target) {
                panic!(
                    "This capability routing seems invalid, the target \
                     does not exist in this realm. Source: {:?} Target: {:?}",
                    capability_provider, capability_target,
                );
            }

            let sibling_deps = dependency_map.get_mut(&capability_provider).expect(&format!(
                "This capability routing seems invalid, the source \
                 does not exist in this realm. Source: {:?} Target: {:?}",
                capability_provider, capability_target,
            ));
            sibling_deps.insert(capability_target);
        }
    }
}

/// Loops through all the child and collection declarations to determine what siblings provide
/// capabilities to other siblings through an environment.
fn get_dependencies_from_environments(decl: &ComponentDecl, dependency_map: &mut DependencyMap) {
    let mut env_source_children = HashMap::new();
    for env in &decl.environments {
        env_source_children.insert(&env.name, vec![]);
        for runner in &env.runners {
            if let RegistrationSource::Child(source_child) = &runner.source {
                env_source_children.get_mut(&env.name).unwrap().push(source_child);
            }
        }
    }

    for dest_child in &decl.children {
        if let Some(env_name) = dest_child.environment.as_ref() {
            for source_child in env_source_children.get(env_name).expect(&format!(
                "environment `{}` from child `{}` is not a valid environment",
                env_name, dest_child.name,
            )) {
                dependency_map
                    .entry(DependencyNode::Child((*source_child).clone()))
                    .or_insert(HashSet::new())
                    .insert(DependencyNode::Child(dest_child.name.clone()));
            }
        }
    }
    for dest_collection in &decl.collections {
        if let Some(env_name) = dest_collection.environment.as_ref() {
            for source_child in env_source_children.get(env_name).expect(&format!(
                "environment `{}` from collection `{}` is not a valid environment",
                env_name, dest_collection.name,
            )) {
                dependency_map
                    .entry(DependencyNode::Child((*source_child).clone()))
                    .or_insert(HashSet::new())
                    .insert(DependencyNode::Collection(dest_collection.name.clone()));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    // Tests for ShutdownJob are found in actions.rs where we try to shutdown
    // various component topologies.
    use {
        super::*,
        crate::model::testing::test_helpers::{
            default_component_decl, ChildDeclBuilder, CollectionDeclBuilder, EnvironmentDeclBuilder,
        },
        anyhow::Error,
        cm_rust::{
            CapabilityName, CapabilityNameOrPath, ChildDecl, DependencyType, ExposeDecl,
            ExposeProtocolDecl, ExposeSource, ExposeTarget, OfferProtocolDecl, OfferResolverDecl,
            OfferServiceSource, OfferTarget,
        },
        fidl_fuchsia_sys2 as fsys,
        std::collections::HashMap,
        std::convert::TryFrom,
    };

    // TODO(jmatt) Add tests for all capability types

    /// Validates that actual looks like expected and panics if they don't.
    /// `expected` must be sorted and so must the second member of each
    /// tuple in the vec.
    fn validate_results(
        expected: Vec<(DependencyNode, Vec<DependencyNode>)>,
        mut actual: HashMap<DependencyNode, HashSet<DependencyNode>>,
    ) {
        let mut actual_sorted: Vec<(DependencyNode, Vec<DependencyNode>)> = actual
            .drain()
            .map(|(k, v)| {
                let mut new_vec = Vec::new();
                new_vec.extend(v.into_iter());
                new_vec.sort_unstable();
                (k, new_vec)
            })
            .collect();
        actual_sorted.sort_unstable();
        assert_eq!(expected, actual_sorted);
    }

    #[test]
    fn test_service_from_parent() -> Result<(), Error> {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferServiceSource::Self_,
                source_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                target_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                target: OfferTarget::Child("childA".to_string()),
                dependency_type: DependencyType::Strong,
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
            }],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((DependencyNode::Child("childA".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_weak_service_from_parent() -> Result<(), Error> {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferServiceSource::Self_,
                source_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                target_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                target: OfferTarget::Child("childA".to_string()),
                dependency_type: DependencyType::WeakForMigration,
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
            }],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((DependencyNode::Child("childA".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_service_from_child() -> Result<(), Error> {
        let decl = ComponentDecl {
            exposes: vec![ExposeDecl::Protocol(ExposeProtocolDecl {
                target: ExposeTarget::Parent,
                source_path: CapabilityNameOrPath::try_from("/svc/serviceFromChild").unwrap(),
                target_path: CapabilityNameOrPath::try_from("/svc/serviceFromChild").unwrap(),
                source: ExposeSource::Child("childA".to_string()),
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
            }],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((DependencyNode::Child("childA".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_single_dependency() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        let mut v = vec![DependencyNode::Child(child_a.name.clone())];
        v.sort_unstable();
        expected.push((DependencyNode::Child(child_b.name.clone()), v));
        expected.push((DependencyNode::Child(child_a.name.clone()), vec![]));
        expected.sort_unstable();

        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_environment_with_runner_from_parent() -> Result<(), Error> {
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

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((DependencyNode::Child("childA".to_string()), vec![]));
        expected.push((DependencyNode::Child("childB".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_environment_with_runner_from_child() -> Result<(), Error> {
        let decl = ComponentDecl {
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
            ],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((
            DependencyNode::Child("childA".to_string()),
            vec![DependencyNode::Child("childB".to_string())],
        ));
        expected.push((DependencyNode::Child("childB".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_environment_with_runner_from_child_to_collection() -> Result<(), Error> {
        let decl = ComponentDecl {
            environments: vec![EnvironmentDeclBuilder::new()
                .name("env")
                .add_runner(cm_rust::RunnerRegistration {
                    source: RegistrationSource::Child("childA".into()),
                    source_name: "foo".into(),
                    target_name: "foo".into(),
                })
                .build()],
            collections: vec![CollectionDeclBuilder::new().name("coll").environment("env").build()],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((
            DependencyNode::Child("childA".to_string()),
            vec![DependencyNode::Collection("coll".to_string())],
        ));
        expected.push((DependencyNode::Collection("coll".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_chained_environments() -> Result<(), Error> {
        let decl = ComponentDecl {
            environments: vec![
                EnvironmentDeclBuilder::new()
                    .name("env")
                    .add_runner(cm_rust::RunnerRegistration {
                        source: RegistrationSource::Child("childA".into()),
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                    })
                    .build(),
                EnvironmentDeclBuilder::new()
                    .name("env2")
                    .add_runner(cm_rust::RunnerRegistration {
                        source: RegistrationSource::Child("childB".into()),
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

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((
            DependencyNode::Child("childA".to_string()),
            vec![DependencyNode::Child("childB".to_string())],
        ));
        expected.push((
            DependencyNode::Child("childB".to_string()),
            vec![DependencyNode::Child("childC".to_string())],
        ));
        expected.push((DependencyNode::Child("childC".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_environment_and_offer() -> Result<(), Error> {
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferServiceSource::Child("childB".to_string()),
                source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                target: OfferTarget::Child("childC".to_string()),
                dependency_type: DependencyType::Strong,
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

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((
            DependencyNode::Child("childA".to_string()),
            vec![DependencyNode::Child("childB".to_string())],
        ));
        expected.push((
            DependencyNode::Child("childB".to_string()),
            vec![DependencyNode::Child("childC".to_string())],
        ));
        expected.push((DependencyNode::Child("childC".to_string()), vec![]));
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_single_weak_dependency() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::WeakForMigration,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::WeakForMigration,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((DependencyNode::Child(child_b.name.clone()), vec![]));
        expected.push((DependencyNode::Child(child_a.name.clone()), vec![]));
        expected.sort_unstable();

        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_multiple_dependencies_same_source() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceParent").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBOtherOffer").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceOtherSibling")
                        .unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        let mut v = vec![DependencyNode::Child(child_a.name.clone())];
        v.sort_unstable();
        expected.push((DependencyNode::Child(child_b.name.clone()), v));
        expected.push((DependencyNode::Child(child_a.name.clone()), vec![]));
        expected.sort_unstable();

        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_multiple_dependents_same_source() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBToC").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone(), child_c.clone()],

            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        let mut v = vec![
            DependencyNode::Child(child_a.name.clone()),
            DependencyNode::Child(child_c.name.clone()),
        ];
        v.sort_unstable();
        expected.push((DependencyNode::Child(child_b.name.clone()), v));
        expected.push((DependencyNode::Child(child_a.name.clone()), vec![]));
        expected.push((DependencyNode::Child(child_c.name.clone()), vec![]));
        expected.sort_unstable();
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_multiple_dependencies() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBToC").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childC".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childCToA").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                    dependency_type: DependencyType::WeakForMigration,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone(), child_c.clone()],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((
            DependencyNode::Child(child_b.name.clone()),
            vec![DependencyNode::Child(child_c.name.clone())],
        ));
        expected.push((
            DependencyNode::Child(child_a.name.clone()),
            vec![DependencyNode::Child(child_c.name.clone())],
        ));
        expected.push((DependencyNode::Child(child_c.name.clone()), vec![]));
        expected.sort_unstable();

        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    fn test_component_is_source_and_target() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childB".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBToC").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
            ],
            children: vec![child_a.clone(), child_b.clone(), child_c.clone()],
            ..default_component_decl()
        };

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();

        expected.push((
            DependencyNode::Child(child_a.name.clone()),
            vec![DependencyNode::Child(child_b.name.clone())],
        ));
        expected.push((
            DependencyNode::Child(child_b.name.clone()),
            vec![DependencyNode::Child(child_c.name.clone())],
        ));
        expected.push((DependencyNode::Child(child_c.name.clone()), vec![]));
        expected.sort_unstable();
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
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
    #[test]
    fn test_complex_routing() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_d = ChildDecl {
            name: "childD".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_e = ChildDecl {
            name: "childE".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childB".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childBService").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/childBService").unwrap(),
                    target: OfferTarget::Child("childD".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childC".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childD".to_string()),
                    dependency_type: DependencyType::Strong,
                }),
                OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("childC".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childE".to_string()),
                    dependency_type: DependencyType::Strong,
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

        let mut expected: Vec<(DependencyNode, Vec<DependencyNode>)> = Vec::new();
        expected.push((
            DependencyNode::Child(child_a.name.clone()),
            vec![
                DependencyNode::Child(child_b.name.clone()),
                DependencyNode::Child(child_c.name.clone()),
            ],
        ));
        expected.push((
            DependencyNode::Child(child_b.name.clone()),
            vec![DependencyNode::Child(child_d.name.clone())],
        ));
        expected.push((
            DependencyNode::Child(child_c.name.clone()),
            vec![
                DependencyNode::Child(child_d.name.clone()),
                DependencyNode::Child(child_e.name.clone()),
            ],
        ));
        expected.push((DependencyNode::Child(child_d.name.clone()), vec![]));
        expected.push((DependencyNode::Child(child_e.name.clone()), vec![]));
        expected.sort_unstable();
        validate_results(expected, process_component_dependencies(&decl));
        Ok(())
    }

    #[test]
    #[should_panic]
    fn test_target_does_not_exist() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        // This declaration is invalid because the offer target doesn't exist
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferServiceSource::Child("childA".to_string()),
                source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                target: OfferTarget::Child("childB".to_string()),
                dependency_type: DependencyType::Strong,
            })],
            children: vec![child_a.clone()],
            ..default_component_decl()
        };

        process_component_dependencies(&decl);
    }

    #[test]
    #[should_panic]
    fn test_source_does_not_exist() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        // This declaration is invalid because the offer target doesn't exist
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferServiceSource::Child("childB".to_string()),
                source_path: CapabilityNameOrPath::try_from("/svc/childBOffer").unwrap(),
                target_path: CapabilityNameOrPath::try_from("/svc/serviceSibling").unwrap(),
                target: OfferTarget::Child("childA".to_string()),
                dependency_type: DependencyType::Strong,
            })],
            children: vec![child_a.clone()],
            ..default_component_decl()
        };

        process_component_dependencies(&decl);
    }

    #[test]
    fn test_resolver_capability_creates_dependency() {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        };
        let decl = ComponentDecl {
            offers: vec![OfferDecl::Resolver(OfferResolverDecl {
                source: OfferResolverSource::Child("childA".to_string()),
                source_name: CapabilityName::try_from("resolver").unwrap(),
                target_name: CapabilityName::try_from("resolver").unwrap(),
                target: OfferTarget::Child("childB".to_string()),
            })],
            children: vec![child_a.clone(), child_b.clone()],
            ..default_component_decl()
        };

        let mut expected = vec![
            (
                DependencyNode::Child(child_a.name.clone()),
                vec![DependencyNode::Child(child_b.name.clone())],
            ),
            (DependencyNode::Child(child_b.name.clone()), vec![]),
        ];
        expected.sort_unstable();
        validate_results(expected, process_component_dependencies(&decl));
    }
}
