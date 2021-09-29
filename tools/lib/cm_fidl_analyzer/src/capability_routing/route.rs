// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_tree::NodePath,
    cm_rust::{CapabilityDecl, CapabilityName, ExposeDecl, OfferDecl, UseDecl},
    routing::RegistrationDecl,
    serde::{Deserialize, Serialize},
};

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
