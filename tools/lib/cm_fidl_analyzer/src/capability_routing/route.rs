// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::capability_routing::source::CapabilitySourceType,
    crate::component_tree::NodePath,
    cm_rust::CapabilityName,
    std::{fmt, fmt::Display},
};

/// Summarizes an action at a single node of a capability route. A
/// full route is a vector of `RouteSegment`s. This type is used to
/// display the result of a route walk by a `CapabilityRouteVerifier`.
#[derive(Clone)]
pub enum RouteSegment {
    /// Parameters: the `NodePath` of the using `ComponentNode`, the
    /// `CapabilityName` by which the capability is used , and the
    /// `CapabilitySourceType` of the capability relative to the using node.
    UseBy(NodePath, CapabilityName, CapabilitySourceType),

    /// Parameters: the `NodePath` of the offering `ComponentNode`, the
    /// `CapabilityName` by which the capability is offered, and the
    /// `CapabilitySourceType` of the capability relative to the offering node.
    OfferBy(NodePath, CapabilityName, CapabilitySourceType),

    /// Parameters: the `NodePath` of the exposing `ComponentNode`, the
    /// `CapabilityName` by which the capability is exposed, and the
    /// `CapabilitySourceType` of the capability relative to the using node.
    ExposeBy(NodePath, CapabilityName, CapabilitySourceType),

    /// Parameters: the `NodePath` of the declaring `ComponentNode` and the
    /// `CapabilityName` by which the capability is declared.
    DeclareBy(NodePath, CapabilityName),

    /// The final source of the capability is the component framework.
    RouteFromFramework,

    /// The final source of the capability is the parent of the root component.
    RouteFromRootParent,
}

impl Display for RouteSegment {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RouteSegment::UseBy(node_path, name, source_type) => {
                write!(f, "use by `{}` as `{}` from {}", node_path, name, source_type)
            }
            RouteSegment::OfferBy(node_path, name, source_type) => {
                write!(f, "offer by `{}` as `{}` from {}", node_path, name, source_type)
            }
            RouteSegment::ExposeBy(node_path, name, source_type) => {
                write!(f, "expose by `{}` as `{}` from {}", node_path, name, source_type)
            }
            RouteSegment::DeclareBy(node_path, name) => {
                write!(f, "declare by `{}` as `{}`", node_path, name)
            }
            RouteSegment::RouteFromFramework => {
                write!(f, "route from framework")
            }
            RouteSegment::RouteFromRootParent => {
                write!(f, "route from root parent")
            }
        }
    }
}
