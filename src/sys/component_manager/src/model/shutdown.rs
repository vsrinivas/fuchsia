// Copyright 2019 The Fuchsia Authors. All right reserved.
// Use of this source code is goverend by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::{
        ComponentDecl, OfferDecl, OfferDirectorySource, OfferRunnerSource, OfferServiceSource,
        OfferStorageSource, OfferTarget, StorageDecl, StorageDirectorySource,
    },
    std::collections::{HashMap, HashSet},
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
/// offer from Self or Realm.
fn find_storage_provider(storage_decls: &Vec<StorageDecl>, name: &str) -> Option<String> {
    for decl in storage_decls {
        if decl.name == name {
            match &decl.source {
                StorageDirectorySource::Child(child) => {
                    return Some(child.to_string());
                }
                StorageDirectorySource::Self_ | StorageDirectorySource::Realm => {
                    return None;
                }
            }
        }
    }
    None
}

/// For a given ComponentDecl, parse it, identify capability dependencies
/// between children and collections in the ComponentDecl. A map is returned
/// which maps from a child to a set of other children to which that child
/// provides capabilities. The siblings to which the child offers capabilities
/// must be shut down before that child. This function panics if there is a
/// capability routing where either the source or target is not present in this
/// ComponentDecl. Panics are not expected because ComponentDecls should be
/// validated before this function is called.
pub fn process_component_dependencies(
    decl: &ComponentDecl,
) -> HashMap<DependencyNode, HashSet<DependencyNode>> {
    let mut children: HashMap<DependencyNode, HashSet<DependencyNode>> = decl
        .children
        .iter()
        .map(|c| (DependencyNode::Child(c.name.clone()), HashSet::new()))
        .collect();
    children.extend(
        decl.collections
            .iter()
            .map(|c| (DependencyNode::Collection(c.name.clone()), HashSet::new())),
    );

    // Loop through all the offer declarations to determine which siblings
    // provide capabilities to other siblings.
    for dep in &decl.offers {
        // Identify the source and target of the offer. We only care about
        // dependencies where the provider of the dependency is another child,
        // otherwise the capability comes from the parent or component manager
        // itself in which case the relationship is not relevant for ordering
        // here.
        let source_target_pairs = match dep {
            OfferDecl::LegacyService(svc_offer) => {
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
                    OfferServiceSource::Self_ | OfferServiceSource::Realm => {
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
                        OfferServiceSource::Self_ | OfferServiceSource::Realm => {
                            // Capabilities offered by the parent or routed in
                            // from the realm are not relevant.
                            continue;
                        }
                    }
                }
                pairs
            }
            OfferDecl::Directory(dir_offer) => {
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
                    | OfferDirectorySource::Realm
                    | OfferDirectorySource::Framework => {
                        // Capabilities offered by the parent or routed in from
                        // the realm are not relevant.
                        continue;
                    }
                }
            }
            OfferDecl::Storage(s) => {
                match s.source() {
                    OfferStorageSource::Storage(source_name) => {
                        match find_storage_provider(&decl.storage, &source_name) {
                            Some(storage_source) => match s.target() {
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
                    OfferStorageSource::Realm => {
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
                    OfferRunnerSource::Self_ | OfferRunnerSource::Realm => {
                        // Capabilities coming from the parent aren't tracked.
                        continue;
                    }
                }
            }
        };

        for (capability_provider, capability_target) in source_target_pairs {
            if !children.contains_key(&capability_target) {
                panic!(
                    "This capability routing seems invalid, the target \
                     does not exist in this realm. Source: {:?} Target: {:?}",
                    capability_provider, capability_target,
                );
            }

            let sibling_deps = children.get_mut(&capability_provider).expect(&format!(
                "This capability routing seems invalid, the source \
                 does not exist in this realm. Source: {:?} Target: {:?}",
                capability_provider, capability_target,
            ));
            sibling_deps.insert(capability_target);
        }
    }

    children
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::testing::test_helpers::default_component_decl,
        cm_rust::{
            CapabilityPath, ChildDecl, ExposeDecl, ExposeLegacyServiceDecl, ExposeSource,
            ExposeTarget, OfferLegacyServiceDecl, OfferServiceSource, OfferTarget,
        },
        failure::Error,
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
        // TOOD(jmatt) convert this function into a macro for improved
        // debugging in panics
        assert_eq!(expected.len(), actual.len());

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
            offers: vec![OfferDecl::LegacyService(OfferLegacyServiceDecl {
                source: OfferServiceSource::Self_,
                source_path: CapabilityPath::try_from("/svc/serviceParent").unwrap(),
                target_path: CapabilityPath::try_from("/svc/serviceParent").unwrap(),
                target: OfferTarget::Child("childA".to_string()),
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fsys::StartupMode::Lazy,
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
            exposes: vec![ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                target: ExposeTarget::Realm,
                source_path: CapabilityPath::try_from("/svc/serviceFromChild").unwrap(),
                target_path: CapabilityPath::try_from("/svc/serviceFromChild").unwrap(),
                source: ExposeSource::Child("childA".to_string()),
            })],
            children: vec![ChildDecl {
                name: "childA".to_string(),
                url: "ignored:///child".to_string(),
                startup: fsys::StartupMode::Lazy,
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
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/serviceParent").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceParent").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
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
    fn test_multiple_dependencies_same_source() -> Result<(), Error> {
        let child_a = ChildDecl {
            name: "childA".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/serviceParent").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceParent").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBOtherOffer").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceOtherSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
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
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childA".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBToC").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
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
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBToC").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
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
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBOffer").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childB".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBToC").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
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
        };
        let child_b = ChildDecl {
            name: "childB".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let child_c = ChildDecl {
            name: "childC".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let child_d = ChildDecl {
            name: "childD".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let child_e = ChildDecl {
            name: "childE".to_string(),
            url: "ignored:///child".to_string(),
            startup: fsys::StartupMode::Lazy,
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childB".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childA".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childC".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childB".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childBService").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/childBService").unwrap(),
                    target: OfferTarget::Child("childD".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childC".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childD".to_string()),
                }),
                OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Child("childC".to_string()),
                    source_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/childAService").unwrap(),
                    target: OfferTarget::Child("childE".to_string()),
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
        };
        // This declaration is invalid because the offer target doesn't exist
        let decl = ComponentDecl {
            offers: vec![OfferDecl::LegacyService(OfferLegacyServiceDecl {
                source: OfferServiceSource::Child("childA".to_string()),
                source_path: CapabilityPath::try_from("/svc/childBOffer").unwrap(),
                target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                target: OfferTarget::Child("childB".to_string()),
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
        };
        // This declaration is invalid because the offer target doesn't exist
        let decl = ComponentDecl {
            offers: vec![OfferDecl::LegacyService(OfferLegacyServiceDecl {
                source: OfferServiceSource::Child("childB".to_string()),
                source_path: CapabilityPath::try_from("/svc/childBOffer").unwrap(),
                target_path: CapabilityPath::try_from("/svc/serviceSibling").unwrap(),
                target: OfferTarget::Child("childA".to_string()),
            })],
            children: vec![child_a.clone()],
            ..default_component_decl()
        };

        process_component_dependencies(&decl);
    }
}
