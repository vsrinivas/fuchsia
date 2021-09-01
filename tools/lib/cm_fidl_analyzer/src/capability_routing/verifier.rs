// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_routing::{
            error::CapabilityRouteError, route::RouteSegment, source::CapabilitySourceType,
        },
        component_tree::{ComponentNode, ComponentTree, NodePath},
    },
    cm_rust::{CapabilityDecl, CapabilityName, ExposeDecl, OfferDecl, UseDecl},
    moniker::PartialChildMoniker,
};

/// Represents the state of a capability route at a node of the v2 `ComponentTree`.
#[derive(Clone)]
pub struct CapabilityRouteState<'a, T> {
    /// The tree of v2 components.
    pub tree: &'a ComponentTree,
    /// The `ComponentNode` associated with the remaining data.
    pub node: &'a ComponentNode,
    /// The name of the capability as designated by the component which
    /// offered or exposed that capability to `node`.
    pub name: CapabilityName,
    /// The relationship of the component which offered or exposed the capability
    /// to `node`.
    pub source_type: CapabilitySourceType,
    /// The route so far, starting at the using node.
    pub route: Vec<RouteSegment>,
    /// Fields specific to validation of a particular capability type.
    pub fields: T,
}

/// A summary of a specific capability route and the outcome of verification.
#[derive(Clone)]
pub struct VerifyRouteResult {
    pub using_node: NodePath,
    pub capability: CapabilityName,
    pub result: Result<Vec<RouteSegment>, CapabilityRouteError>,
}

/// The `CapabilityRouteVerifier` trait defines an interface and some common
/// methods for types that verify routing for a specific capability type.
pub trait CapabilityRouteVerifier<'a> {
    // TODO(https://fxbug.dev/68907): consider adding bounds to these
    // types with some of the cm_rust traits e.g.
    // {Use,Offer,Expose,Capability}CommonDecl and SourceName. With
    // some minor modifications to those traits, most of the
    // capability-type-specific implementation could be moved into
    // this trait definition.
    type UseDeclType: 'a + Clone + Into<UseDecl>;
    type OfferDeclType: 'a + Clone + Into<OfferDecl>;
    type ExposeDeclType: 'a + Clone + Into<ExposeDecl>;
    type CapabilityDeclType: 'a + Clone + Into<CapabilityDecl>;
    type FieldsType;

    /// Verifies that a specific capability is routed and used correctly.
    /// Checks routing for the capability described in `use_decl` and used
    /// by the component corresponding to `using_node`, returning a representation
    /// of the route if valid or an error if not.
    fn verify_route(
        &self,
        tree: &'a ComponentTree,
        use_decl: &'a Self::UseDeclType,
        using_node: &'a ComponentNode,
    ) -> Result<Vec<RouteSegment>, CapabilityRouteError> {
        let mut target_state = self.state_from_use(tree, use_decl, using_node)?;
        let mut source_state = self.get_source_state(&target_state)?;

        while !self.is_final(&source_state.source_type) {
            self.verify_route_segment(&target_state, &source_state)?;
            target_state = source_state;
            source_state = self.get_source_state(&target_state)?;
        }
        Ok(source_state.route)
    }

    /// Verifies routing for all uses of `Self::CapabilityDeclType` capabilities by `using_node`.
    fn verify_all_routes(
        &'a self,
        tree: &'a ComponentTree,
        using_node: &'a ComponentNode,
    ) -> Vec<VerifyRouteResult> {
        let mut results = Vec::<VerifyRouteResult>::new();

        for use_decl in self.get_use_decls(using_node).iter() {
            let (name, _) = self.get_use_info(use_decl);

            results.push(VerifyRouteResult {
                using_node: using_node.node_path().clone(),
                capability: name,
                result: self.verify_route(tree, use_decl, using_node),
            });
        }

        results
    }

    /// Returns a bool saying whether `source_type` signals the end of a route.
    fn is_final(&self, source_type: &CapabilitySourceType) -> bool {
        match source_type {
            CapabilitySourceType::Framework => true,
            CapabilitySourceType::RootParent => true,
            CapabilitySourceType::Decl => true,
            _ => false,
        }
    }

    /// Creates and returns the source state from the source declared in `target_state`
    /// or returns an error if the source is malformed or incorrectly specified.
    fn get_source_state(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
    ) -> Result<CapabilityRouteState<'a, Self::FieldsType>, CapabilityRouteError> {
        match &target_state.source_type {
            CapabilitySourceType::Parent => {
                match &target_state.tree.try_get_parent(&target_state.node)? {
                    Some(parent) => {
                        let offer_decl = self.find_offer_decl(target_state, &parent)?;
                        self.state_from_offer(target_state, offer_decl, &parent)
                    }
                    // `target_state.node` is the root node.
                    None => self.state_from_root_parent(target_state),
                }
            }
            CapabilitySourceType::Child(child_name) => {
                let child = &target_state.tree.get_child_node(
                    &target_state.node,
                    PartialChildMoniker::new(child_name.clone(), None),
                )?;
                let expose_decl = self.find_expose_decl(target_state, &child)?;
                self.state_from_expose(target_state, expose_decl, &child)
            }
            CapabilitySourceType::Self_ => {
                let cap_decl = self.find_capability_decl(target_state, target_state.node)?;
                self.state_from_declare(target_state, cap_decl)
            }
            CapabilitySourceType::Framework => self.state_from_framework(target_state),
            CapabilitySourceType::Capability(_) => {
                Err(CapabilityRouteError::ValidationNotImplemented(
                    "source of type Capability".to_string(),
                ))
            }
            _ => Err(CapabilityRouteError::InvalidSourceType(target_state.source_type.to_string())),
        }
    }

    /// Creates and returns a `CapabilityRouteState` from a using declaration, or an error if the
    /// using declaration is invalid.
    fn state_from_use(
        &self,
        tree: &'a ComponentTree,
        use_decl: &'a Self::UseDeclType,
        using_node: &'a ComponentNode,
    ) -> Result<CapabilityRouteState<'a, Self::FieldsType>, CapabilityRouteError> {
        let (name, source_type) = self.get_use_info(use_decl);

        match source_type {
            CapabilitySourceType::Capability(_) => {
                Err(CapabilityRouteError::ValidationNotImplemented(
                    "source of type Capability".to_string(),
                ))
            }
            CapabilitySourceType::Debug => {
                Err(CapabilityRouteError::InvalidSourceType(source_type.to_string()))
            }
            _ => Ok(CapabilityRouteState::<Self::FieldsType> {
                tree: tree,
                node: using_node,
                name: name.clone(),
                source_type: source_type.clone(),
                route: vec![RouteSegment::UseBy {
                    node_path: using_node.node_path(),
                    capability: use_decl.clone().into(),
                }],
                fields: self.fields_from_use(&use_decl)?,
            }),
        }
    }

    /// Creates and returns a `CapabilityRouteState` from an offer declaration, or an error if the
    /// offer declaration is invalid.
    fn state_from_offer(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        offer_decl: &'a Self::OfferDeclType,
        offering_node: &'a ComponentNode,
    ) -> Result<CapabilityRouteState<'a, Self::FieldsType>, CapabilityRouteError> {
        let (_target_name, source_name, source_type) = self.get_offer_info(offer_decl);

        let mut route = target_state.route.clone();
        route.push(RouteSegment::OfferBy {
            node_path: offering_node.node_path(),
            capability: offer_decl.clone().into(),
        });

        Ok(CapabilityRouteState::<Self::FieldsType> {
            tree: target_state.tree,
            node: offering_node,
            name: source_name,
            source_type: source_type,
            route,
            fields: self.fields_from_offer(target_state, offer_decl)?,
        })
    }

    /// Creates and returns a `CapabilityRouteState` from an expose declaration, or an error if the
    /// expose declaration is invalid.
    fn state_from_expose(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        expose_decl: &'a Self::ExposeDeclType,
        exposing_node: &'a ComponentNode,
    ) -> Result<CapabilityRouteState<'a, Self::FieldsType>, CapabilityRouteError> {
        let (_target_name, source_name, source_type) = self.get_expose_info(expose_decl);

        if let CapabilitySourceType::Capability(_) = source_type {
            return Err(CapabilityRouteError::ValidationNotImplemented(
                "source of type Capability".to_string(),
            ));
        }

        let mut route = target_state.route.clone();
        route.push(RouteSegment::ExposeBy {
            node_path: exposing_node.node_path(),
            capability: expose_decl.clone().into(),
        });

        Ok(CapabilityRouteState::<Self::FieldsType> {
            tree: target_state.tree,
            node: exposing_node,
            name: source_name,
            route,
            source_type: source_type,
            fields: self.fields_from_expose(target_state, expose_decl)?,
        })
    }

    /// Creates and returns a `CapabilityRouteState` from a capability declaration, or an error if the
    /// capability declaration is invalid.
    fn state_from_declare(
        &self,
        route_state: &CapabilityRouteState<'a, Self::FieldsType>,
        capability_decl: &'a Self::CapabilityDeclType,
    ) -> Result<CapabilityRouteState<'a, Self::FieldsType>, CapabilityRouteError> {
        let name = self.get_declare_info(capability_decl);

        let mut route = route_state.route.clone();
        route.push(RouteSegment::DeclareBy {
            node_path: route_state.node.node_path(),
            capability: capability_decl.clone().into(),
        });

        Ok(CapabilityRouteState::<Self::FieldsType> {
            tree: route_state.tree,
            node: route_state.node,
            name: name.clone(),
            source_type: CapabilitySourceType::Decl,
            route,
            fields: self.fields_from_declare(capability_decl)?,
        })
    }

    /// Creates and returns a `CapabilityRouteState` for a claimed routing from the component framework,
    /// or an error if the routing is invalid.
    fn state_from_framework(
        &self,
        route_state: &CapabilityRouteState<'a, Self::FieldsType>,
    ) -> Result<CapabilityRouteState<'a, Self::FieldsType>, CapabilityRouteError> {
        let mut route = route_state.route.clone();
        route.push(RouteSegment::RouteFromFramework);

        Ok(CapabilityRouteState::<Self::FieldsType> {
            tree: route_state.tree,
            node: route_state.node,
            name: route_state.name.clone(),
            source_type: CapabilitySourceType::Framework,
            route,
            fields: self.fields_from_framework(route_state)?,
        })
    }

    /// Creates and returns a `CapabilityRouteState` for a claimed routing from the root parent,
    /// or an error if the routing is invalid.
    fn state_from_root_parent(
        &self,
        route_state: &CapabilityRouteState<'a, Self::FieldsType>,
    ) -> Result<CapabilityRouteState<'a, Self::FieldsType>, CapabilityRouteError> {
        let mut route = route_state.route.clone();
        route.push(RouteSegment::RouteFromRootParent);

        Ok(CapabilityRouteState::<Self::FieldsType> {
            tree: route_state.tree,
            node: route_state.node,
            name: route_state.name.clone(),
            source_type: CapabilitySourceType::RootParent,
            route,
            fields: self.fields_from_root_parent(route_state)?,
        })
    }

    /// Finds an offer declaration by `source_node` which matches the data in `target_state`,
    /// or returns an error if no matching offer exists or if duplicate offers exist.
    fn find_offer_decl(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        source_node: &'a ComponentNode,
    ) -> Result<&'a Self::OfferDeclType, CapabilityRouteError> {
        // Only the root node has a moniker of None, and this method should never be
        // called with the root node as the target.
        if target_state.node.moniker().is_none() {
            return Err(CapabilityRouteError::Internal(
                "attempted to find offer to root node".to_string(),
            ));
        }
        let target_moniker = target_state.node.moniker().unwrap();
        let decls = self.get_offer_decls(source_node);

        let matches: Vec<&&Self::OfferDeclType> = decls
            .iter()
            .filter(|decl| self.is_matching_offer(target_state, target_moniker, decl))
            .collect();

        match matches.len() {
            0 => Err(CapabilityRouteError::OfferDeclNotFound(
                source_node.short_display(),
                target_state.name.to_string(),
            )),
            1 => Ok(*matches[0]),
            _ => Err(CapabilityRouteError::DuplicateOfferDecl(
                source_node.short_display(),
                target_state.name.to_string(),
            )),
        }
    }

    /// Finds an expose declaration by `source_node` which matches the data in `target_state`,
    /// or returns an error if no matching declaration exists or if duplicate declarations exist.
    fn find_expose_decl(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        source_node: &'a ComponentNode,
    ) -> Result<&'a Self::ExposeDeclType, CapabilityRouteError> {
        let decls = self.get_expose_decls(source_node);

        let matches: Vec<&&Self::ExposeDeclType> =
            decls.iter().filter(|decl| self.is_matching_expose(target_state, decl)).collect();
        match matches.len() {
            0 => Err(CapabilityRouteError::ExposeDeclNotFound(
                source_node.short_display(),
                target_state.name.to_string(),
            )),
            1 => Ok(*matches[0]),
            _ => Err(CapabilityRouteError::DuplicateExposeDecl(
                source_node.short_display(),
                target_state.name.to_string(),
            )),
        }
    }

    /// Finds a capability declaration by `node` which matches the data in `route_state`, or returns
    /// an error if no matching declaration exists or if duplicate declarations exist.
    fn find_capability_decl(
        &self,
        route_state: &CapabilityRouteState<'a, Self::FieldsType>,
        node: &'a ComponentNode,
    ) -> Result<&'a Self::CapabilityDeclType, CapabilityRouteError> {
        let decls = self.get_capability_decls(node);

        let matches: Vec<&&Self::CapabilityDeclType> =
            decls.iter().filter(|decl| self.is_matching_declare(route_state, decl)).collect();
        match matches.len() {
            0 => Err(CapabilityRouteError::CapabilityDeclNotFound(
                node.short_display(),
                route_state.name.to_string(),
            )),
            1 => Ok(*matches[0]),
            _ => Err(CapabilityRouteError::DuplicateCapabilityDecl(
                node.short_display(),
                route_state.name.to_string(),
            )),
        }
    }

    // Methods specific to the type of capability.
    //
    // TODO(https://fxbug.dev/68907): With a few changes to cm_rust
    // traits, most of these could be implemented generically.  Only
    // verify_route_segment() and the fields_from_*() methods really
    // need type-specific implementations.

    /// Checks the relationship between `target_state` and `source_state` against
    /// a policy for this capability type.
    fn verify_route_segment(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        source_state: &CapabilityRouteState<'a, Self::FieldsType>,
    ) -> Result<(), CapabilityRouteError>;

    // Methods specific to Self::UseDeclType

    /// Returns a copy of the source name and source type from `use_decl`.
    fn get_use_info(&self, use_decl: &Self::UseDeclType) -> (CapabilityName, CapabilitySourceType);

    /// Returns a vector all of the declarations of type `Self::UseDeclType` found in `node`'s
    /// `ComponentDecl`.
    fn get_use_decls(&self, node: &'a ComponentNode) -> Vec<&'a Self::UseDeclType>;

    // Methods specific to Self::OfferDeclType

    /// Returns a copy of the target name, source name, and source type (in that order) from `offer_decl`.
    fn get_offer_info(
        &self,
        offer_decl: &Self::OfferDeclType,
    ) -> (CapabilityName, CapabilityName, CapabilitySourceType);

    /// Returns a vector all of the declarations of type `Self::OfferDeclType` found in `node`'s
    /// `ComponentDecl`.
    fn get_offer_decls(&self, node: &'a ComponentNode) -> Vec<&'a Self::OfferDeclType>;

    /// Returns a bool saying whether `offer_decl` matches the offer specified by `target_state` and
    /// `target_moniker`; i.e., whether `offer_decl` is an offer to a child with moniker `target_moniker`
    /// of the capability specified by `target_state`.
    fn is_matching_offer(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        target_moniker: &'a PartialChildMoniker,
        offer_decl: &'a Self::OfferDeclType,
    ) -> bool;

    // Methods specific to Self::ExposeDeclType

    /// Returns a copy of the target name, source name, and source type (in that order) from `expose_decl`.
    fn get_expose_info(
        &self,
        expose_decl: &Self::ExposeDeclType,
    ) -> (CapabilityName, CapabilityName, CapabilitySourceType);

    /// Returns a vector all of the declarations of type `Self::ExposeDeclType` found in `node`'s
    /// `ComponentDecl`.
    fn get_expose_decls(&self, node: &'a ComponentNode) -> Vec<&'a Self::ExposeDeclType>;

    /// Returns a bool saying whether `expose_decl` matches the routing specified by `target_state`.
    fn is_matching_expose(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        expose_decl: &'a Self::ExposeDeclType,
    ) -> bool;

    // Methods specific to Self::CapabilityDeclType

    /// Returns a copy of the capability name from `capability_decl`.
    fn get_declare_info(&self, capability_decl: &Self::CapabilityDeclType) -> CapabilityName;

    /// Returns a vector all of the declarations of type `Self::CapabilityDeclType` found in `node`'s
    /// `ComponentDecl`.
    fn get_capability_decls(&self, node: &'a ComponentNode) -> Vec<&'a Self::CapabilityDeclType>;

    /// Returns a bool saying whether a capability declaration `decl` matches the routing specified by
    /// `route_state`.
    fn is_matching_declare(
        &self,
        route_state: &CapabilityRouteState<'a, Self::FieldsType>,
        decl: &'a Self::CapabilityDeclType,
    ) -> bool;

    // Methods specific to Self::FieldsType

    /// Populates a `Self::FieldsType` from `use_decl`, or returns an error if `use_decl` is invalid.
    fn fields_from_use(
        &self,
        use_decl: &'a Self::UseDeclType,
    ) -> Result<Self::FieldsType, CapabilityRouteError>;

    /// Populates a `Self::FieldsType` from `offer_decl`, or returns an error if `offer_decl` is invalid.
    fn fields_from_offer(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        offer_decl: &'a Self::OfferDeclType,
    ) -> Result<Self::FieldsType, CapabilityRouteError>;

    /// Populates a `Self::FieldsType` from `expose_decl`, or returns an error if `expose_decl` is invalid.
    fn fields_from_expose(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
        expose_decl: &'a Self::ExposeDeclType,
    ) -> Result<Self::FieldsType, CapabilityRouteError>;

    /// Populates a `Self::FieldsType` from `capability_decl`, or returns an error if `capability_decl`
    /// is invalid.
    fn fields_from_declare(
        &self,
        capability_decl: &'a Self::CapabilityDeclType,
    ) -> Result<Self::FieldsType, CapabilityRouteError>;

    /// Populates a `Self::FieldsType` from a claimed route from the framework, or returns an error if this
    /// claim is invalid.
    fn fields_from_framework(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
    ) -> Result<Self::FieldsType, CapabilityRouteError>;

    /// Populates a `Self::FieldsType` from a claimed route from the root parent, or returns an error if this
    /// claim is invalid.
    fn fields_from_root_parent(
        &self,
        target_state: &CapabilityRouteState<'a, Self::FieldsType>,
    ) -> Result<Self::FieldsType, CapabilityRouteError>;
}
