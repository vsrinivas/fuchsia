// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_routing::{
            error::CapabilityRouteError,
            source::CapabilitySourceType,
            verifier::{CapabilityRouteState, CapabilityRouteVerifier},
        },
        component_tree::ComponentNode,
    },
    cm_rust::{
        CapabilityDecl, CapabilityName, ChildRef, ExposeDecl, ExposeProtocolDecl, ExposeTarget,
        OfferDecl, OfferProtocolDecl, OfferTarget, ProtocolDecl, UseDecl, UseProtocolDecl,
    },
    moniker::{ChildMonikerBase, PartialChildMoniker},
};

/// A verifier for protocol capability routes.
pub struct ProtocolCapabilityRouteVerifier {}

impl ProtocolCapabilityRouteVerifier {
    pub fn new() -> Self {
        ProtocolCapabilityRouteVerifier {}
    }
}

pub struct ProtocolFields {}

impl<'a> CapabilityRouteVerifier<'a> for ProtocolCapabilityRouteVerifier {
    type UseDeclType = UseProtocolDecl;
    type OfferDeclType = OfferProtocolDecl;
    type ExposeDeclType = ExposeProtocolDecl;
    type CapabilityDeclType = ProtocolDecl;
    type FieldsType = ProtocolFields;

    fn verify_route_segment(
        &self,
        _target_state: &CapabilityRouteState<'a, ProtocolFields>,
        _source_state: &CapabilityRouteState<'a, ProtocolFields>,
    ) -> Result<(), CapabilityRouteError> {
        Ok(())
    }

    // Implementations specific to `UseProtocolDecl`

    fn get_use_info(&self, use_decl: &UseProtocolDecl) -> (CapabilityName, CapabilitySourceType) {
        (use_decl.source_name.clone(), CapabilitySourceType::from(&use_decl.source))
    }

    fn get_use_decls(&self, node: &'a ComponentNode) -> Vec<&'a UseProtocolDecl> {
        let mut use_dir_decls = Vec::<&UseProtocolDecl>::new();
        for decl in node.decl.uses.iter() {
            if let UseDecl::Protocol(use_dir_decl) = decl {
                use_dir_decls.push(use_dir_decl);
            }
        }
        use_dir_decls
    }

    // Implementations specific to `OfferProtocolDecl`

    fn get_offer_info(
        &self,
        offer_decl: &OfferProtocolDecl,
    ) -> (CapabilityName, CapabilityName, CapabilitySourceType) {
        (
            offer_decl.target_name.clone(),
            offer_decl.source_name.clone(),
            CapabilitySourceType::from(&offer_decl.source),
        )
    }

    fn get_offer_decls(&self, node: &'a ComponentNode) -> Vec<&'a OfferProtocolDecl> {
        let mut offer_dir_decls = Vec::<&OfferProtocolDecl>::new();
        for decl in node.decl.offers.iter() {
            if let OfferDecl::Protocol(offer_dir_decl) = decl {
                offer_dir_decls.push(offer_dir_decl);
            }
        }
        offer_dir_decls
    }

    fn is_matching_offer(
        &self,
        target_state: &CapabilityRouteState<'a, ProtocolFields>,
        target_moniker: &'a PartialChildMoniker,
        offer_decl: &'a OfferProtocolDecl,
    ) -> bool {
        if let OfferTarget::Child(ChildRef { name: child_name, collection }) = &offer_decl.target {
            // This crate only runs over static `ComponentDecl`s, where dynamic
            // offers don't make sense.
            assert_eq!(
                *collection, None,
                "A dynamic child appeared in an OfferDecl contained in a ComponentDecl: {:?}",
                offer_decl
            );
            return (&child_name.as_str(), &offer_decl.target_name)
                == (&target_moniker.as_str(), &target_state.name);
        }
        false
    }

    // Implementations specific to `ExposeProtocolDecl`

    fn get_expose_info(
        &self,
        expose_decl: &ExposeProtocolDecl,
    ) -> (CapabilityName, CapabilityName, CapabilitySourceType) {
        (
            expose_decl.target_name.clone(),
            expose_decl.source_name.clone(),
            CapabilitySourceType::from(&expose_decl.source),
        )
    }

    fn get_expose_decls(&self, node: &'a ComponentNode) -> Vec<&'a ExposeProtocolDecl> {
        let mut expose_dir_decls = Vec::<&ExposeProtocolDecl>::new();
        for decl in node.decl.exposes.iter() {
            if let ExposeDecl::Protocol(expose_dir_decl) = decl {
                expose_dir_decls.push(expose_dir_decl);
            }
        }
        expose_dir_decls
    }

    fn is_matching_expose(
        &self,
        target_state: &CapabilityRouteState<'a, ProtocolFields>,
        expose_decl: &'a ExposeProtocolDecl,
    ) -> bool {
        if ExposeTarget::Parent == expose_decl.target {
            return expose_decl.target_name == target_state.name;
        }
        false
    }

    // Implementations specific to `ProtocolDecl`

    fn get_declare_info(&self, capability_decl: &ProtocolDecl) -> CapabilityName {
        capability_decl.name.clone()
    }

    fn get_capability_decls(&self, node: &'a ComponentNode) -> Vec<&'a ProtocolDecl> {
        let mut dir_decls = Vec::<&ProtocolDecl>::new();
        for decl in node.decl.capabilities.iter() {
            if let CapabilityDecl::Protocol(dir_decl) = decl {
                dir_decls.push(dir_decl);
            }
        }
        dir_decls
    }

    fn is_matching_declare(
        &self,
        route_state: &CapabilityRouteState<'a, ProtocolFields>,
        decl: &'a ProtocolDecl,
    ) -> bool {
        decl.name == route_state.name
    }

    // Implementations specific to `ProtocolFields`

    fn fields_from_use(
        &self,
        _use_decl: &UseProtocolDecl,
    ) -> Result<ProtocolFields, CapabilityRouteError> {
        Ok(ProtocolFields {})
    }

    fn fields_from_offer(
        &self,
        _target_state: &CapabilityRouteState<'a, ProtocolFields>,
        _offer_decl: &'a OfferProtocolDecl,
    ) -> Result<ProtocolFields, CapabilityRouteError> {
        Ok(ProtocolFields {})
    }

    fn fields_from_expose(
        &self,
        _target_state: &CapabilityRouteState<'a, ProtocolFields>,
        _expose_decl: &'a ExposeProtocolDecl,
    ) -> Result<ProtocolFields, CapabilityRouteError> {
        Ok(ProtocolFields {})
    }

    fn fields_from_declare(
        &self,
        _capability_decl: &'a ProtocolDecl,
    ) -> Result<ProtocolFields, CapabilityRouteError> {
        Ok(ProtocolFields {})
    }

    fn fields_from_framework(
        &self,
        _target_state: &CapabilityRouteState<'a, ProtocolFields>,
    ) -> Result<ProtocolFields, CapabilityRouteError> {
        Ok(ProtocolFields {})
    }

    fn fields_from_root_parent(
        &self,
        _target_state: &CapabilityRouteState<'a, ProtocolFields>,
    ) -> Result<ProtocolFields, CapabilityRouteError> {
        Ok(ProtocolFields {})
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            capability_routing::{route::RouteSegment, testing::*},
            component_tree::{BuildTreeResult, ComponentTreeBuilder, NodeEnvironment, NodePath},
        },
        cm_rust::{CapabilityPath, DependencyType, ExposeSource, OfferSource, UseSource},
        moniker::PartialChildMoniker,
        routing::environment::{DebugRegistry, RunnerRegistry},
        std::collections::HashMap,
    };

    fn new_use_protocol_decl(source: UseSource, source_name: CapabilityName) -> UseProtocolDecl {
        UseProtocolDecl {
            source,
            source_name,
            target_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
            dependency_type: DependencyType::Strong,
        }
    }

    fn new_offer_protocol_decl(
        source: OfferSource,
        source_name: CapabilityName,
        target: OfferTarget,
        target_name: CapabilityName,
    ) -> OfferProtocolDecl {
        OfferProtocolDecl {
            source,
            source_name,
            target,
            target_name,
            dependency_type: DependencyType::Strong,
        }
    }

    fn new_expose_protocol_decl(
        source: ExposeSource,
        source_name: CapabilityName,
        target: ExposeTarget,
        target_name: CapabilityName,
    ) -> ExposeProtocolDecl {
        ExposeProtocolDecl { source, source_name, target, target_name }
    }

    fn new_protocol_decl(name: CapabilityName) -> ProtocolDecl {
        ProtocolDecl { name, source_path: None }
    }

    // Builds a `ComponentTree` with 4 nodes and the following structure:
    //
    //          root
    //         /    \
    //       foo    bar
    //       /
    //     baz
    //
    // In addition, adds routing for the protocol capability specified in `use_decl`.
    // The capability is provided by `bar` and used by `baz` via `root` and `foo`. Each node
    // defines its own alias for the capability.
    fn build_tree_with_protocol_route() -> BuildTreeResult {
        let root_url = "root_url".to_string();
        let foo_url = "foo_url".to_string();
        let bar_url = "bar_url".to_string();
        let baz_url = "baz_url".to_string();

        let foo_name = "foo".to_string();
        let bar_name = "bar".to_string();
        let baz_name = "baz".to_string();

        let root_protocol_name = CapabilityName("root_protocol_name".to_string());
        let foo_protocol_name = CapabilityName("foo_protocol_name".to_string());
        let bar_protocol_name = CapabilityName("bar_protocol_name".to_string());
        let baz_protocol_name = CapabilityName("baz_protocol_name".to_string());

        let bar_decl = new_component_decl(
            vec![],
            vec![ExposeDecl::Protocol(new_expose_protocol_decl(
                ExposeSource::Self_,
                bar_protocol_name.clone(),
                ExposeTarget::Parent,
                root_protocol_name.clone(),
            ))],
            vec![],
            vec![CapabilityDecl::Protocol(new_protocol_decl(bar_protocol_name))],
            vec![],
        );
        let root_decl = new_component_decl(
            vec![],
            vec![],
            vec![OfferDecl::Protocol(new_offer_protocol_decl(
                OfferSource::static_child(bar_name.clone()),
                root_protocol_name,
                OfferTarget::static_child(foo_name.clone()),
                foo_protocol_name.clone(),
            ))],
            vec![],
            vec![new_child_decl(&foo_name, &foo_url), new_child_decl(&bar_name, &bar_url)],
        );
        let foo_decl = new_component_decl(
            vec![],
            vec![],
            vec![OfferDecl::Protocol(new_offer_protocol_decl(
                OfferSource::Parent,
                foo_protocol_name,
                OfferTarget::static_child(baz_name.clone()),
                baz_protocol_name.clone(),
            ))],
            vec![],
            vec![new_child_decl(&baz_name, &baz_url)],
        );
        let baz_decl = new_component_decl(
            vec![UseDecl::Protocol(new_use_protocol_decl(UseSource::Parent, baz_protocol_name))],
            vec![],
            vec![],
            vec![],
            vec![],
        );

        let mut decls = HashMap::new();
        decls.insert(root_url.to_string(), root_decl.clone());
        decls.insert(foo_url.to_string(), foo_decl.clone());
        decls.insert(bar_url.to_string(), bar_decl.clone());
        decls.insert(baz_url.to_string(), baz_decl.clone());

        ComponentTreeBuilder::new(decls).build(
            root_url,
            NodeEnvironment::new_root(RunnerRegistry::default(), DebugRegistry::default()),
        )
    }

    // Checks that `ProtocolCapabilityRouteVerifier::verify_route()` accepts a valid 2-node route.
    #[test]
    fn protocol_verify_offer_from_parent() -> Result<(), CapabilityRouteError> {
        let verifier = ProtocolCapabilityRouteVerifier::new();
        let child_name = "child".to_string();
        let protocol_name = CapabilityName("protocol_name".to_string());

        let root_offer_protocol = new_offer_protocol_decl(
            OfferSource::Self_,
            protocol_name.clone(),
            OfferTarget::static_child(child_name.clone()),
            protocol_name.clone(),
        );
        let root_protocol_decl = new_protocol_decl(protocol_name.clone());

        let child_use_protocol = new_use_protocol_decl(UseSource::Parent, protocol_name);

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![OfferDecl::Protocol(root_offer_protocol)],
            vec![CapabilityDecl::Protocol(root_protocol_decl)],
            vec![UseDecl::Protocol(child_use_protocol.clone())],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let route = verifier.verify_route(&tree, &child_use_protocol, &child_node)?;
        assert_eq!(
            route,
            vec![
                RouteSegment::UseBy {
                    node_path: node_path(vec!["child"]),
                    capability: UseProtocolDecl {
                        source_name: "protocol_name".into(),
                        ..default_use_protocol()
                    }
                    .into(),
                },
                RouteSegment::OfferBy {
                    node_path: node_path(vec![]),
                    capability: OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "protocol_name".into(),
                        target: OfferTarget::static_child("child".to_string()),
                        target_name: "protocol_name".into(),
                        ..default_offer_protocol()
                    }
                    .into(),
                },
                RouteSegment::DeclareBy {
                    node_path: node_path(vec![]),
                    capability: ProtocolDecl {
                        name: "protocol_name".into(),
                        ..default_declare_protocol()
                    }
                    .into(),
                },
            ]
        );

        Ok(())
    }

    // Checks that `ProtocolCapabilityRouteVerifier::verify_route()` rejects a route when
    // a child node has a `UseProtocolDecl` with a source of Parent, but its parent has no
    // matching `OfferProtocolDecl`.
    #[test]
    fn protocol_verify_missing_offer() -> Result<(), CapabilityRouteError> {
        let verifier = ProtocolCapabilityRouteVerifier::new();
        let child_name = "child".to_string();
        let protocol_name = CapabilityName("protocol_name".to_string());
        let child_use_protocol = new_use_protocol_decl(UseSource::Parent, protocol_name.clone());

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![],
            vec![],
            vec![UseDecl::Protocol(child_use_protocol.clone())],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let verify_result = verifier.verify_route(&tree, &child_use_protocol, &child_node);
        assert!(verify_result.is_err());
        assert_eq!(
            verify_result.err().unwrap(),
            CapabilityRouteError::OfferDeclNotFound("/".to_string(), protocol_name.to_string())
        );

        Ok(())
    }

    // Checks that `ProtocolCapabilityRouteVerifier::verify_route()` rejects a route when
    // a child node has a `UseProtocolDecl` with a source of Parent, and the parent node
    // has a matching `OfferProtocolDecl` with a source of Self but no matching
    // `ProtocolDecl.`
    #[test]
    fn protocol_verify_missing_capability() -> Result<(), CapabilityRouteError> {
        let verifier = ProtocolCapabilityRouteVerifier::new();
        let child_name = "child".to_string();
        let protocol_name = CapabilityName("protocol_name".to_string());

        let root_offer_protocol = new_offer_protocol_decl(
            OfferSource::Self_,
            protocol_name.clone(),
            OfferTarget::static_child(child_name.clone()),
            protocol_name.clone(),
        );

        let child_use_protocol = new_use_protocol_decl(UseSource::Parent, protocol_name.clone());

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![OfferDecl::Protocol(root_offer_protocol)],
            vec![],
            vec![UseDecl::Protocol(child_use_protocol.clone())],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let verify_result = verifier.verify_route(&tree, &child_use_protocol, &child_node);
        assert!(verify_result.is_err());
        assert_eq!(
            verify_result.err().unwrap(),
            CapabilityRouteError::CapabilityDeclNotFound(
                "/".to_string(),
                protocol_name.to_string()
            )
        );

        Ok(())
    }

    // Checks that `ProtocolCapabilityRouteVerifier::verify_route()` accepts a valid 4-node route
    // consisting of a use, two offers, and one expose with matching definition.
    #[test]
    fn protocol_verify_4_node_route() -> Result<(), CapabilityRouteError> {
        let verifier = ProtocolCapabilityRouteVerifier::new();

        let build_tree_result = build_tree_with_protocol_route();
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let using_node = tree.get_node(&NodePath::new(vec![
            PartialChildMoniker::new("foo".to_string(), None),
            PartialChildMoniker::new("baz".to_string(), None),
        ]))?;
        assert_eq!(using_node.decl.uses.len(), 1);
        let use_decl = &using_node.decl.uses[0];

        if let UseDecl::Protocol(use_protocol_decl) = use_decl {
            let route = verifier.verify_route(&tree, &use_protocol_decl, &using_node)?;
            assert_eq!(
                route,
                vec![
                    RouteSegment::UseBy {
                        node_path: node_path(vec!["foo", "baz"]),
                        capability: UseProtocolDecl {
                            source_name: "baz_protocol_name".into(),
                            ..default_use_protocol()
                        }
                        .into(),
                    },
                    RouteSegment::OfferBy {
                        node_path: node_path(vec!["foo"]),
                        capability: OfferProtocolDecl {
                            source_name: "foo_protocol_name".into(),
                            target: OfferTarget::static_child("baz".to_string()),
                            target_name: "baz_protocol_name".into(),
                            ..default_offer_protocol()
                        }
                        .into(),
                    },
                    RouteSegment::OfferBy {
                        node_path: node_path(vec![]),
                        capability: OfferProtocolDecl {
                            source: OfferSource::static_child("bar".to_string()),
                            source_name: "root_protocol_name".into(),
                            target: OfferTarget::static_child("foo".to_string()),
                            target_name: "foo_protocol_name".into(),
                            ..default_offer_protocol()
                        }
                        .into(),
                    },
                    RouteSegment::ExposeBy {
                        node_path: node_path(vec!["bar"]),
                        capability: ExposeProtocolDecl {
                            source_name: "bar_protocol_name".into(),
                            target_name: "root_protocol_name".into(),
                            ..default_expose_protocol()
                        }
                        .into(),
                    },
                    RouteSegment::DeclareBy {
                        node_path: node_path(vec!["bar"]),
                        capability: ProtocolDecl {
                            name: "bar_protocol_name".into(),
                            ..default_declare_protocol()
                        }
                        .into(),
                    },
                ]
            );
            Ok(())
        } else {
            panic!["Failed precondition: expected UseDecl of type Protocol"]
        }
    }

    // Checks that `ProtocolCapabilityRouteVerifier::verify_all_routes()` accepts a valid route and
    // rejects an invalid route.
    #[test]
    fn protocol_verify_all_routes() -> Result<(), CapabilityRouteError> {
        let verifier = ProtocolCapabilityRouteVerifier::new();

        let child_name = "child".to_string();

        let good_protocol_name = CapabilityName("good_protocol".to_string());
        let bad_protocol_name = CapabilityName("bad_protocol".to_string());

        let good_offer_protocol = new_offer_protocol_decl(
            OfferSource::Self_,
            good_protocol_name.clone(),
            OfferTarget::static_child(child_name.clone()),
            good_protocol_name.clone(),
        );

        let good_protocol_decl = new_protocol_decl(good_protocol_name.clone());
        let bad_protocol_decl = new_protocol_decl(bad_protocol_name.clone());

        let good_use_protocol =
            new_use_protocol_decl(UseSource::Parent, good_protocol_name.clone());
        let bad_use_protocol = new_use_protocol_decl(UseSource::Parent, bad_protocol_name.clone());

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![OfferDecl::Protocol(good_offer_protocol)],
            vec![
                CapabilityDecl::Protocol(good_protocol_decl),
                CapabilityDecl::Protocol(bad_protocol_decl),
            ],
            vec![UseDecl::Protocol(good_use_protocol), UseDecl::Protocol(bad_use_protocol)],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let mut results = verifier.verify_all_routes(&tree, &child_node);

        assert_eq!(results.len(), 2);
        let good_result = results.remove(0);
        let bad_result = results.remove(0);

        assert_eq!(good_result.using_node.to_string(), "/child");
        assert_eq!(good_result.capability, good_protocol_name);
        assert!(good_result.result.is_ok());
        let good_route = good_result.result.as_ref().unwrap();
        assert_eq!(
            good_route,
            &vec![
                RouteSegment::UseBy {
                    node_path: node_path(vec!["child"]),
                    capability: UseProtocolDecl {
                        source_name: "good_protocol".into(),
                        ..default_use_protocol()
                    }
                    .into(),
                },
                RouteSegment::OfferBy {
                    node_path: node_path(vec![]),
                    capability: OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "good_protocol".into(),
                        target: OfferTarget::static_child("child".to_string()),
                        target_name: "good_protocol".into(),
                        ..default_offer_protocol()
                    }
                    .into(),
                },
                RouteSegment::DeclareBy {
                    node_path: node_path(vec![]),
                    capability: ProtocolDecl {
                        name: "good_protocol".into(),
                        ..default_declare_protocol()
                    }
                    .into(),
                },
            ]
        );

        assert_eq!(bad_result.using_node.to_string(), "/child");
        assert_eq!(bad_result.capability, bad_protocol_name);
        assert!(bad_result.result.is_err());
        assert_eq!(
            *bad_result.result.err().as_ref().unwrap(),
            CapabilityRouteError::OfferDeclNotFound("/".to_string(), "bad_protocol".to_string())
        );

        Ok(())
    }
}
