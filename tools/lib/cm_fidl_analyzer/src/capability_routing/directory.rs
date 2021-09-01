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
        CapabilityDecl, CapabilityName, DirectoryDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeTarget, OfferDecl, OfferDirectoryDecl, OfferTarget, UseDecl, UseDirectoryDecl,
    },
    fidl_fuchsia_io2::Operations,
    moniker::{ChildMonikerBase, PartialChildMoniker},
};

/// A verifier for directory capability routes and rights.
pub struct DirectoryCapabilityRouteVerifier {}

impl DirectoryCapabilityRouteVerifier {
    pub fn new() -> Self {
        DirectoryCapabilityRouteVerifier {}
    }
}

pub struct DirectoryFields {
    rights: Operations,
}

impl<'a> CapabilityRouteVerifier<'a> for DirectoryCapabilityRouteVerifier {
    type UseDeclType = UseDirectoryDecl;
    type OfferDeclType = OfferDirectoryDecl;
    type ExposeDeclType = ExposeDirectoryDecl;
    type CapabilityDeclType = DirectoryDecl;
    type FieldsType = DirectoryFields;

    /// Checks that the directory rights specified in `target_state` are a
    /// subset of those specified in `source_state`.
    fn verify_route_segment(
        &self,
        target_state: &CapabilityRouteState<'a, DirectoryFields>,
        source_state: &CapabilityRouteState<'a, DirectoryFields>,
    ) -> Result<(), CapabilityRouteError> {
        if !source_state.fields.rights.contains(target_state.fields.rights) {
            return Err(CapabilityRouteError::InvalidDirectoryRights(
                source_state.node.short_display(),
            ));
        }
        Ok(())
    }

    // Implementations specific to `UseDirectoryDecl`

    fn get_use_info(&self, use_decl: &UseDirectoryDecl) -> (CapabilityName, CapabilitySourceType) {
        (use_decl.source_name.clone(), CapabilitySourceType::from(&use_decl.source))
    }

    fn get_use_decls(&self, node: &'a ComponentNode) -> Vec<&'a UseDirectoryDecl> {
        let mut use_dir_decls = Vec::<&UseDirectoryDecl>::new();
        for decl in node.decl.uses.iter() {
            if let UseDecl::Directory(use_dir_decl) = decl {
                use_dir_decls.push(use_dir_decl);
            }
        }
        use_dir_decls
    }

    // Implementations specific to `OfferDirectoryDecl`

    fn get_offer_info(
        &self,
        offer_decl: &OfferDirectoryDecl,
    ) -> (CapabilityName, CapabilityName, CapabilitySourceType) {
        (
            offer_decl.target_name.clone(),
            offer_decl.source_name.clone(),
            CapabilitySourceType::from(&offer_decl.source),
        )
    }

    fn get_offer_decls(&self, node: &'a ComponentNode) -> Vec<&'a OfferDirectoryDecl> {
        let mut offer_dir_decls = Vec::<&OfferDirectoryDecl>::new();
        for decl in node.decl.offers.iter() {
            if let OfferDecl::Directory(offer_dir_decl) = decl {
                offer_dir_decls.push(offer_dir_decl);
            }
        }
        offer_dir_decls
    }

    fn is_matching_offer(
        &self,
        target_state: &CapabilityRouteState<'a, DirectoryFields>,
        target_moniker: &'a PartialChildMoniker,
        offer_decl: &'a OfferDirectoryDecl,
    ) -> bool {
        if let OfferTarget::Child(child_name) = &offer_decl.target {
            return (&child_name.as_str(), &offer_decl.target_name)
                == (&target_moniker.as_str(), &target_state.name);
        }
        false
    }

    // Implementations specific to `ExposeDirectoryDecl`

    fn get_expose_info(
        &self,
        expose_decl: &ExposeDirectoryDecl,
    ) -> (CapabilityName, CapabilityName, CapabilitySourceType) {
        (
            expose_decl.target_name.clone(),
            expose_decl.source_name.clone(),
            CapabilitySourceType::from(&expose_decl.source),
        )
    }

    fn get_expose_decls(&self, node: &'a ComponentNode) -> Vec<&'a ExposeDirectoryDecl> {
        let mut expose_dir_decls = Vec::<&ExposeDirectoryDecl>::new();
        for decl in node.decl.exposes.iter() {
            if let ExposeDecl::Directory(expose_dir_decl) = decl {
                expose_dir_decls.push(expose_dir_decl);
            }
        }
        expose_dir_decls
    }

    fn is_matching_expose(
        &self,
        target_state: &CapabilityRouteState<'a, DirectoryFields>,
        expose_decl: &'a ExposeDirectoryDecl,
    ) -> bool {
        if ExposeTarget::Parent == expose_decl.target {
            return expose_decl.target_name == target_state.name;
        }
        false
    }

    // Implementations specific to `DirectoryDecl`

    fn get_declare_info(&self, capability_decl: &DirectoryDecl) -> CapabilityName {
        capability_decl.name.clone()
    }

    fn get_capability_decls(&self, node: &'a ComponentNode) -> Vec<&'a DirectoryDecl> {
        let mut dir_decls = Vec::<&DirectoryDecl>::new();
        for decl in node.decl.capabilities.iter() {
            if let CapabilityDecl::Directory(dir_decl) = decl {
                dir_decls.push(dir_decl);
            }
        }
        dir_decls
    }

    fn is_matching_declare(
        &self,
        route_state: &CapabilityRouteState<'a, DirectoryFields>,
        decl: &'a DirectoryDecl,
    ) -> bool {
        decl.name == route_state.name
    }

    // Implementations specific to `DirectoryFields`

    fn fields_from_use(
        &self,
        use_decl: &UseDirectoryDecl,
    ) -> Result<DirectoryFields, CapabilityRouteError> {
        Ok(DirectoryFields { rights: use_decl.rights })
    }

    fn fields_from_offer(
        &self,
        target_state: &CapabilityRouteState<'a, DirectoryFields>,
        offer_decl: &'a OfferDirectoryDecl,
    ) -> Result<DirectoryFields, CapabilityRouteError> {
        if let Some(offer_rights) = offer_decl.rights {
            return Ok(DirectoryFields { rights: offer_rights });
        }
        Ok(DirectoryFields { rights: target_state.fields.rights })
    }

    fn fields_from_expose(
        &self,
        target_state: &CapabilityRouteState<'a, DirectoryFields>,
        expose_decl: &'a ExposeDirectoryDecl,
    ) -> Result<DirectoryFields, CapabilityRouteError> {
        if let Some(expose_rights) = expose_decl.rights {
            return Ok(DirectoryFields { rights: expose_rights });
        }
        Ok(DirectoryFields { rights: target_state.fields.rights })
    }

    fn fields_from_declare(
        &self,
        capability_decl: &'a DirectoryDecl,
    ) -> Result<DirectoryFields, CapabilityRouteError> {
        Ok(DirectoryFields { rights: capability_decl.rights })
    }

    fn fields_from_framework(
        &self,
        target_state: &CapabilityRouteState<'a, DirectoryFields>,
    ) -> Result<DirectoryFields, CapabilityRouteError> {
        Ok(DirectoryFields { rights: target_state.fields.rights })
    }

    fn fields_from_root_parent(
        &self,
        target_state: &CapabilityRouteState<'a, DirectoryFields>,
    ) -> Result<DirectoryFields, CapabilityRouteError> {
        Ok(DirectoryFields { rights: target_state.fields.rights })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            capability_routing::{route::RouteSegment, testing::*},
            component_tree::{BuildTreeResult, ComponentTreeBuilder, NodePath},
        },
        cm_rust::{CapabilityPath, DependencyType, ExposeSource, OfferSource, UseSource},
        fidl_fuchsia_io2 as fio2,
        moniker::PartialChildMoniker,
        std::collections::HashMap,
    };

    fn new_use_directory_decl(
        source: UseSource,
        source_name: CapabilityName,
        rights: Operations,
    ) -> UseDirectoryDecl {
        UseDirectoryDecl {
            source,
            source_name,
            target_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
            rights,
            subdir: None,
            dependency_type: DependencyType::Strong,
        }
    }

    fn new_offer_directory_decl(
        source: OfferSource,
        source_name: CapabilityName,
        target: OfferTarget,
        target_name: CapabilityName,
        rights: Option<Operations>,
    ) -> OfferDirectoryDecl {
        OfferDirectoryDecl {
            source,
            source_name,
            target,
            target_name,
            rights,
            subdir: None,
            dependency_type: DependencyType::Strong,
        }
    }

    fn new_expose_directory_decl(
        source: ExposeSource,
        source_name: CapabilityName,
        target: ExposeTarget,
        target_name: CapabilityName,
        rights: Option<Operations>,
    ) -> ExposeDirectoryDecl {
        ExposeDirectoryDecl { source, source_name, target, target_name, rights, subdir: None }
    }

    fn new_directory_decl(name: CapabilityName, rights: Operations) -> DirectoryDecl {
        DirectoryDecl {
            name,
            source_path: None,
            rights,
        }
    }

    // Builds a `ComponentTree` with 4 nodes and the following structure:
    //
    //          root
    //         /    \
    //       foo    bar
    //       /
    //     baz
    //
    // In addition, adds routing for the directory capability specified in `use_decl` with
    // rights specified by the arguments.
    //
    // The capability is provided by `bar` and used by `baz` via `root` and `foo`. Each node
    // defines its own alias for the capability.
    fn build_tree_with_directory_route(
        baz_rights: Operations,
        foo_rights: Option<Operations>,
        root_rights: Option<Operations>,
        bar_rights: Option<Operations>,
    ) -> BuildTreeResult {
        let root_url = "root_url".to_string();
        let foo_url = "foo_url".to_string();
        let bar_url = "bar_url".to_string();
        let baz_url = "baz_url".to_string();

        let foo_name = "foo".to_string();
        let bar_name = "bar".to_string();
        let baz_name = "baz".to_string();

        let root_dir_name = CapabilityName("root_dir_name".to_string());
        let foo_dir_name = CapabilityName("foo_dir_name".to_string());
        let bar_dir_name = CapabilityName("bar_dir_name".to_string());
        let baz_dir_name = CapabilityName("baz_dir_name".to_string());

        let bar_decl = new_component_decl(
            vec![],
            vec![ExposeDecl::Directory(new_expose_directory_decl(
                ExposeSource::Self_,
                bar_dir_name.clone(),
                ExposeTarget::Parent,
                root_dir_name.clone(),
                bar_rights,
            ))],
            vec![],
            vec![CapabilityDecl::Directory(new_directory_decl(bar_dir_name, bar_rights.unwrap()))],
            vec![],
        );
        let root_decl = new_component_decl(
            vec![],
            vec![],
            vec![OfferDecl::Directory(new_offer_directory_decl(
                OfferSource::Child(bar_name.clone()),
                root_dir_name,
                OfferTarget::Child(foo_name.clone()),
                foo_dir_name.clone(),
                root_rights,
            ))],
            vec![],
            vec![new_child_decl(&foo_name, &foo_url), new_child_decl(&bar_name, &bar_url)],
        );
        let foo_decl = new_component_decl(
            vec![],
            vec![],
            vec![OfferDecl::Directory(new_offer_directory_decl(
                OfferSource::Parent,
                foo_dir_name,
                OfferTarget::Child(baz_name.clone()),
                baz_dir_name.clone(),
                foo_rights,
            ))],
            vec![],
            vec![new_child_decl(&baz_name, &baz_url)],
        );
        let baz_decl = new_component_decl(
            vec![UseDecl::Directory(new_use_directory_decl(
                UseSource::Parent,
                baz_dir_name,
                baz_rights,
            ))],
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

        ComponentTreeBuilder::new(decls).build(root_url)
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_route()` accepts a valid 2-node route
    // where source and target specify the same set of rights.
    #[test]
    fn directory_verify_offer_from_parent() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();
        let child_name = "child".to_string();
        let dir_name = CapabilityName("dir_name".to_string());
        let offer_rights = Operations::Connect;

        let root_offer_dir = new_offer_directory_decl(
            OfferSource::Self_,
            dir_name.clone(),
            OfferTarget::Child(child_name.clone()),
            dir_name.clone(),
            Some(offer_rights),
        );
        let root_dir_decl = new_directory_decl(dir_name.clone(), offer_rights);

        let child_use_dir = new_use_directory_decl(UseSource::Parent, dir_name, offer_rights);

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![OfferDecl::Directory(root_offer_dir)],
            vec![CapabilityDecl::Directory(root_dir_decl)],
            vec![UseDecl::Directory(child_use_dir.clone())],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let route = verifier.verify_route(&tree, &child_use_dir, &child_node)?;
        assert_eq!(
            route,
            vec![
                RouteSegment::UseBy {
                    node_path: node_path(vec!["child"]),
                    capability: UseDirectoryDecl {
                        source_name: "dir_name".into(),
                        ..default_use_dir()
                    }
                    .into(),
                },
                RouteSegment::OfferBy {
                    node_path: node_path(vec![]),
                    capability: OfferDirectoryDecl {
                        source: OfferSource::Self_,
                        source_name: "dir_name".into(),
                        target: OfferTarget::Child("child".to_string()),
                        target_name: "dir_name".into(),
                        ..default_offer_dir()
                    }
                    .into(),
                },
                RouteSegment::DeclareBy {
                    node_path: node_path(vec![]),
                    capability: DirectoryDecl { name: "dir_name".into(), ..default_declare_dir() }
                        .into(),
                },
            ]
        );

        Ok(())
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_route()` rejects a route when
    // a child node has a `UseDirectoryDecl` with a source of Parent, but its parent has no
    // matching `OfferDirectoryDecl`.
    #[test]
    fn directory_verify_missing_offer() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();
        let child_name = "child".to_string();
        let dir_name = CapabilityName("dir_name".to_string());
        let child_use_dir =
            new_use_directory_decl(UseSource::Parent, dir_name.clone(), Operations::Connect);

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![],
            vec![],
            vec![UseDecl::Directory(child_use_dir.clone())],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let verify_result = verifier.verify_route(&tree, &child_use_dir, &child_node);
        assert!(verify_result.is_err());
        assert_eq!(
            verify_result.err().unwrap(),
            CapabilityRouteError::OfferDeclNotFound("/".to_string(), dir_name.to_string())
        );

        Ok(())
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_route()` rejects a route when
    // a child node has a `UseDirectoryDecl` with a source of Parent, and the parent node
    // has a matching `OfferDirectoryDecl` with a source of Self but no matching
    // `DirectoryDecl.`
    #[test]
    fn directory_verify_missing_capability() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();
        let child_name = "child".to_string();
        let dir_name = CapabilityName("dir_name".to_string());

        let root_offer_dir = new_offer_directory_decl(
            OfferSource::Self_,
            dir_name.clone(),
            OfferTarget::Child(child_name.clone()),
            dir_name.clone(),
            None,
        );

        let child_use_dir =
            new_use_directory_decl(UseSource::Parent, dir_name.clone(), Operations::Connect);

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![OfferDecl::Directory(root_offer_dir)],
            vec![],
            vec![UseDecl::Directory(child_use_dir.clone())],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let verify_result = verifier.verify_route(&tree, &child_use_dir, &child_node);
        assert!(verify_result.is_err());
        assert_eq!(
            verify_result.err().unwrap(),
            CapabilityRouteError::CapabilityDeclNotFound("/".to_string(), dir_name.to_string())
        );

        Ok(())
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_route()` rejects a 2-node route when
    // the target claims broader rights than the source.
    #[test]
    fn directory_verify_invalid_rights() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();
        let child_name = "child".to_string();
        let dir_name = CapabilityName("dir_name".to_string());
        let offer_rights = Operations::Connect;
        let use_rights = offer_rights | Operations::ReadBytes;

        let root_offer_dir = new_offer_directory_decl(
            OfferSource::Self_,
            dir_name.clone(),
            OfferTarget::Child(child_name.clone()),
            dir_name.clone(),
            Some(offer_rights),
        );

        let child_use_dir = new_use_directory_decl(UseSource::Parent, dir_name, use_rights);

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![OfferDecl::Directory(root_offer_dir)],
            vec![],
            vec![UseDecl::Directory(child_use_dir.clone())],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let child_node =
            tree.get_node(&NodePath::new(vec![PartialChildMoniker::new(child_name, None)]))?;
        let verify_result = verifier.verify_route(&tree, &child_use_dir, &child_node);
        assert!(verify_result.is_err());
        assert_eq!(
            verify_result.err().unwrap(),
            CapabilityRouteError::InvalidDirectoryRights("/".to_string())
        );

        Ok(())
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_route()` accepts a 4-node route consisting of
    // a use, two offers, and one expose when each node on the route specifies the same rights.
    #[test]
    fn directory_verify_route_same_rights() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();
        let rights = Operations::Connect;

        let build_tree_result =
            build_tree_with_directory_route(rights, Some(rights), Some(rights), Some(rights));
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let using_node = tree.get_node(&NodePath::new(vec![
            PartialChildMoniker::new("foo".to_string(), None),
            PartialChildMoniker::new("baz".to_string(), None),
        ]))?;
        assert_eq!(using_node.decl.uses.len(), 1);
        let use_decl = &using_node.decl.uses[0];

        if let UseDecl::Directory(use_dir_decl) = use_decl {
            let route = verifier.verify_route(&tree, &use_dir_decl, &using_node)?;
            assert_eq!(
                route,
                vec![
                    RouteSegment::UseBy {
                        node_path: node_path(vec!["foo", "baz"]),
                        capability: UseDirectoryDecl {
                            source_name: "baz_dir_name".into(),
                            ..default_use_dir()
                        }
                        .into(),
                    },
                    RouteSegment::OfferBy {
                        node_path: node_path(vec!["foo"]),
                        capability: OfferDirectoryDecl {
                            source_name: "foo_dir_name".into(),
                            target: OfferTarget::Child("baz".to_string()),
                            target_name: "baz_dir_name".into(),
                            ..default_offer_dir()
                        }
                        .into(),
                    },
                    RouteSegment::OfferBy {
                        node_path: node_path(vec![]),
                        capability: OfferDirectoryDecl {
                            source: OfferSource::Child("bar".to_string()),
                            source_name: "root_dir_name".into(),
                            target: OfferTarget::Child("foo".to_string()),
                            target_name: "foo_dir_name".into(),
                            ..default_offer_dir()
                        }
                        .into(),
                    },
                    RouteSegment::ExposeBy {
                        node_path: node_path(vec!["bar"]),
                        capability: ExposeDirectoryDecl {
                            source_name: "bar_dir_name".into(),
                            target_name: "root_dir_name".into(),
                            ..default_expose_dir()
                        }
                        .into(),
                    },
                    RouteSegment::DeclareBy {
                        node_path: node_path(vec!["bar"]),
                        capability: DirectoryDecl {
                            name: "bar_dir_name".into(),
                            ..default_declare_dir()
                        }
                        .into(),
                    },
                ]
            );
            Ok(())
        } else {
            panic!["Failed precondition: expected UseDecl of type Directory"]
        }
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_route()` accepts a 4-node route consisting of
    // a use, two offers, and one expose when the using node specifies narrower rights than the
    // ultimate source node.
    #[test]
    fn directory_verify_route_narrowed_rights() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();
        let use_rights = Operations::Connect;
        let provide_rights = use_rights | Operations::ReadBytes;

        let build_tree_result =
            build_tree_with_directory_route(use_rights, None, None, Some(provide_rights));
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let using_node = tree.get_node(&node_path(vec!["foo", "baz"]))?;
        assert_eq!(using_node.decl.uses.len(), 1);
        let use_decl = &using_node.decl.uses[0];

        if let UseDecl::Directory(use_dir_decl) = use_decl {
            let route = verifier.verify_route(&tree, &use_dir_decl, &using_node)?;
            assert_eq!(
                route,
                vec![
                    RouteSegment::UseBy {
                        node_path: node_path(vec!["foo", "baz"]),
                        capability: UseDirectoryDecl {
                            source_name: "baz_dir_name".into(),
                            ..default_use_dir()
                        }
                        .into(),
                    },
                    RouteSegment::OfferBy {
                        node_path: node_path(vec!["foo"]),
                        capability: OfferDirectoryDecl {
                            source_name: "foo_dir_name".into(),
                            target: OfferTarget::Child("baz".to_string()),
                            target_name: "baz_dir_name".into(),
                            rights: None,
                            ..default_offer_dir()
                        }
                        .into(),
                    },
                    RouteSegment::OfferBy {
                        node_path: node_path(vec![]),
                        capability: OfferDirectoryDecl {
                            source: OfferSource::Child("bar".to_string()),
                            source_name: "root_dir_name".into(),
                            target: OfferTarget::Child("foo".to_string()),
                            target_name: "foo_dir_name".into(),
                            rights: None,
                            ..default_offer_dir()
                        }
                        .into(),
                    },
                    RouteSegment::ExposeBy {
                        node_path: node_path(vec!["bar"]),
                        capability: ExposeDirectoryDecl {
                            source_name: "bar_dir_name".into(),
                            target_name: "root_dir_name".into(),
                            rights: Some(fio2::Operations::Connect | fio2::Operations::ReadBytes),
                            ..default_expose_dir()
                        }
                        .into(),
                    },
                    RouteSegment::DeclareBy {
                        node_path: node_path(vec!["bar"]),
                        capability: DirectoryDecl {
                            name: "bar_dir_name".into(),
                            rights: fio2::Operations::Connect | fio2::Operations::ReadBytes,
                            ..default_declare_dir()
                        }
                        .into(),
                    },
                ]
            );
            Ok(())
        } else {
            panic!["Failed precondition: expected UseDecl of type Directory"]
        }
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_route()` rejects a 4-node route consisting of
    // a use, two offers, and one expose when the ultimate source node specifies narrower rights than
    // using node.
    #[test]
    fn directory_verify_route_expanded_rights() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();
        let provide_rights = Operations::Connect;
        let use_rights = provide_rights | Operations::ReadBytes;

        let build_tree_result =
            build_tree_with_directory_route(use_rights, None, None, Some(provide_rights));
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();

        let using_node = tree.get_node(&NodePath::new(vec![
            PartialChildMoniker::new("foo".to_string(), None),
            PartialChildMoniker::new("baz".to_string(), None),
        ]))?;
        assert_eq!(using_node.decl.uses.len(), 1);
        let use_decl = &using_node.decl.uses[0];

        if let UseDecl::Directory(use_dir_decl) = use_decl {
            let verify_result = verifier.verify_route(&tree, &use_dir_decl, &using_node);
            assert!(verify_result.is_err());
            assert_eq!(
                verify_result.err().unwrap(),
                CapabilityRouteError::InvalidDirectoryRights("/bar".to_string())
            );
            Ok(())
        } else {
            panic!["Failed precondition: expected UseDecl of type Directory"]
        }
    }

    // Checks that `DirectoryCapabilityRouteVerifier::verify_all_routes()` accepts a valid route and
    // rejects an invalid route.
    #[test]
    fn directory_verify_all_routes() -> Result<(), CapabilityRouteError> {
        let verifier = DirectoryCapabilityRouteVerifier::new();

        let child_name = "child".to_string();
        let offer_rights = Operations::Connect;
        let bad_use_rights = offer_rights | Operations::ReadBytes;

        let good_dir_name = CapabilityName("good_dir".to_string());
        let bad_dir_name = CapabilityName("bad_dir".to_string());

        let good_offer_dir = new_offer_directory_decl(
            OfferSource::Self_,
            good_dir_name.clone(),
            OfferTarget::Child(child_name.clone()),
            good_dir_name.clone(),
            Some(offer_rights),
        );
        let bad_offer_dir = new_offer_directory_decl(
            OfferSource::Self_,
            bad_dir_name.clone(),
            OfferTarget::Child(child_name.clone()),
            bad_dir_name.clone(),
            Some(offer_rights),
        );

        let good_directory_decl = new_directory_decl(good_dir_name.clone(), offer_rights);
        let bad_directory_decl = new_directory_decl(bad_dir_name.clone(), offer_rights);

        let good_use_dir =
            new_use_directory_decl(UseSource::Parent, good_dir_name.clone(), offer_rights);
        let bad_use_dir =
            new_use_directory_decl(UseSource::Parent, bad_dir_name.clone(), bad_use_rights);

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![OfferDecl::Directory(good_offer_dir), OfferDecl::Directory(bad_offer_dir)],
            vec![
                CapabilityDecl::Directory(good_directory_decl),
                CapabilityDecl::Directory(bad_directory_decl),
            ],
            vec![UseDecl::Directory(good_use_dir), UseDecl::Directory(bad_use_dir)],
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
        assert_eq!(good_result.capability, good_dir_name);
        assert!(good_result.result.is_ok());
        let good_route = good_result.result.as_ref().unwrap();
        assert_eq!(
            good_route,
            &vec![
                RouteSegment::UseBy {
                    node_path: node_path(vec!["child"]),
                    capability: UseDirectoryDecl {
                        source_name: "good_dir".into(),
                        ..default_use_dir()
                    }
                    .into(),
                },
                RouteSegment::OfferBy {
                    node_path: node_path(vec![]),
                    capability: OfferDirectoryDecl {
                        source: OfferSource::Self_,
                        source_name: "good_dir".into(),
                        target: OfferTarget::Child("child".to_string()),
                        target_name: "good_dir".into(),
                        ..default_offer_dir()
                    }
                    .into(),
                },
                RouteSegment::DeclareBy {
                    node_path: node_path(vec![]),
                    capability: DirectoryDecl { name: "good_dir".into(), ..default_declare_dir() }
                        .into(),
                },
            ]
        );
        assert_eq!(bad_result.using_node.to_string(), "/child");
        assert_eq!(bad_result.capability, bad_dir_name);
        assert!(bad_result.result.is_err());
        assert_eq!(
            *bad_result.result.err().as_ref().unwrap(),
            CapabilityRouteError::InvalidDirectoryRights("/".to_string())
        );

        Ok(())
    }
}
