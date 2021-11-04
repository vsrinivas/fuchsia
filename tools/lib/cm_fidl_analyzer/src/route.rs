// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        component_model::{AnalyzerModelError, BuildAnalyzerModelError},
        node_path::NodePath,
    },
    cm_rust::{CapabilityDecl, CapabilityName, ExposeDecl, OfferDecl, UseDecl},
    fuchsia_zircon_status as zx_status,
    moniker::AbsoluteMoniker,
    routing::{DebugRouteMapper, RegistrationDecl},
    serde::{Deserialize, Serialize},
    thiserror::Error,
};

/// A summary of a specific capability route and the outcome of verification.
#[derive(Clone, Debug, PartialEq)]
pub struct VerifyRouteResult {
    pub using_node: NodePath,
    pub capability: CapabilityName,
    pub result: Result<Vec<RouteSegment>, CapabilityRouteError>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "snake_case", tag = "action")]
pub enum RouteSegment {
    /// A `ComponentNode` uses the routed capability.
    UseBy {
        /// The `NodePath` of the using `ComponentNode`.
        node_path: NodePath,
        /// The use declaration from the `ComponentNode`'s manifest.
        capability: UseDecl,
    },

    /// A `ComponentNode` requires a runner in its `ProgramDecl`.
    RequireRunner {
        /// The `NodePath` of the component instance that requires the runner.
        node_path: NodePath,
        /// The name of the required runner.
        runner: CapabilityName,
    },

    /// A `ComponentNode` requires the resolver capability to resolve a child component URL.
    RequireResolver {
        /// The `NodePath` of the component node that requires the resolver.
        node_path: NodePath,
        /// The URL scheme of the resolver.
        scheme: String,
    },

    /// A `ComponentNode` offers the routed capability.
    OfferBy {
        /// The `NodePath` of the offering `ComponentNode`.
        node_path: NodePath,
        /// The offer declaration from the `ComponentNode`'s manifest.
        capability: OfferDecl,
    },

    /// A `ComponentNode` exposes the routed capability.
    ExposeBy {
        /// The `NodePath` of the offering `ComponentNode`.
        node_path: NodePath,
        /// The expose declaration from the `ComponentNode`'s manifest.
        capability: ExposeDecl,
    },

    /// A `ComponentNode` declares the routed capability.
    DeclareBy {
        /// The `NodePath` of the declaring `ComponentNode`.
        node_path: NodePath,
        /// The capability declaration from the `ComponentNode`'s manifest.
        capability: CapabilityDecl,
    },

    RegisterBy {
        /// The `NodePath` of the `ComponentNode` that registered the capability.
        node_path: NodePath,
        /// The registration declaration. For runner and resolver registrations, this
        /// appears directly in the `ComponentNode`'s manifest. For storage-backing
        /// directories, this is derived from the storage capability's `StorageDecl`.
        capability: RegistrationDecl,
    },

    /// The source of the capability is the component framework.
    ProvideFromFramework {
        capability: CapabilityName,
    },

    /// The capability is provided by component manager as a built-in capability.
    ProvideAsBuiltin {
        /// The capability declaration from the `RuntimeConfig`.
        capability: CapabilityDecl,
    },

    /// The capability is provided by component manager from its namespace.
    ProvideFromNamespace {
        /// The capability declaration from the `RuntimeConfig`.
        capability: CapabilityDecl,
    },

    // Deprecated. Use `ProvideFromFramework` instead.
    RouteFromFramework,
    // Deprecated. Use one of `ProvideAsBuiltin` or `ProvideFromNamespace` instead.
    RouteFromRootParent,
}

impl RouteSegment {
    pub fn node_path<'a>(&'a self) -> Option<&'a NodePath> {
        match self {
            Self::UseBy { node_path, .. }
            | Self::RequireRunner { node_path, .. }
            | Self::RequireResolver { node_path, .. }
            | Self::OfferBy { node_path, .. }
            | Self::ExposeBy { node_path, .. }
            | Self::DeclareBy { node_path, .. }
            | Self::RegisterBy { node_path, .. } => Some(node_path),
            Self::ProvideFromFramework { .. }
            | Self::ProvideAsBuiltin { .. }
            | Self::ProvideFromNamespace { .. }
            | Self::RouteFromFramework
            | Self::RouteFromRootParent => None,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Error, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CapabilityRouteError {
    #[error("failed to find component: `{0}`")]
    ComponentNotFound(BuildAnalyzerModelError),
    #[error("no offer declaration for `{0}` with name `{1}`")]
    OfferDeclNotFound(String, String),
    #[error("multiple offer declarations found for `{0}` with name `{1}`")]
    DuplicateOfferDecl(String, String),
    #[error("no expose declaration for `{0}` with name `{1}`")]
    ExposeDeclNotFound(String, String),
    #[error("multiple expose declarations found for `{0}` with name `{1}`")]
    DuplicateExposeDecl(String, String),
    #[error("no capability declaration for `{0}` with name `{1}`")]
    CapabilityDeclNotFound(String, String),
    #[error("multiple capability declarations found for `{0}` with name `{1}`")]
    DuplicateCapabilityDecl(String, String),
    #[error("directory rights provided by `{0}` are not sufficient")]
    InvalidDirectoryRights(String),
    #[error("invalid source type: `{0}`")]
    InvalidSourceType(String),
    #[error("validation is not implemented for case: {0}")]
    ValidationNotImplemented(String),
    #[error("unexpected verifier state: {0}")]
    Internal(String),

    // Temporarily nest AnalyzerModelError under CapabilityRouteError during the
    // transistion to the ComponentModelForAnalyzer-based static analyzer.
    //
    // TODO(https://fxbug.dev/61861): Replace CapabilityRouteError with AnalyzerModelError
    // once the transition is complete.
    #[error(transparent)]
    AnalyzerModelError(#[from] AnalyzerModelError),
}

impl CapabilityRouteError {
    pub fn as_zx_status(&self) -> zx_status::Status {
        match self {
            Self::AnalyzerModelError(err) => err.as_zx_status(),
            _ => zx_status::Status::INTERNAL,
        }
    }
}

impl From<BuildAnalyzerModelError> for CapabilityRouteError {
    fn from(err: BuildAnalyzerModelError) -> Self {
        CapabilityRouteError::ComponentNotFound(err)
    }
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

impl Into<Vec<RouteSegment>> for RouteMap {
    fn into(self) -> Vec<RouteSegment> {
        self.0
    }
}

/// A struct implementing `DebugRouteMapper` that records a `RouteMap` as the router
/// walks a capability route.
#[derive(Clone, Debug)]
pub struct RouteMapper {
    route: RouteMap,
}

impl RouteMapper {
    pub fn new() -> Self {
        Self { route: RouteMap::new() }
    }
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
