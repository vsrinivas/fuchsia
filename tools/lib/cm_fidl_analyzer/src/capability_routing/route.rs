// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_tree::NodePath,
    cm_rust::{CapabilityDecl, ExposeDecl, OfferDecl, UseDecl},
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

    /// The final source of the capability is the component framework.
    RouteFromFramework,

    /// The final source of the capability is the parent of the root component.
    RouteFromRootParent,
}
