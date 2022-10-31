// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use num_derive::FromPrimitive;

/// Represents a Thread device role.
///
/// Functional equivalent of [`otsys::otBackboneRouterMulticastListenerEvent`](crate::otsys::otBackboneRouterMulticastListenerEvent).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, FromPrimitive)]
#[allow(missing_docs)]
pub enum BackboneRouterMulticastListenerEvent {
    /// Functional equivalent of [`otsys::OT_BACKBONE_ROUTER_MULTICAST_LISTENER_ADDED`](crate::otsys::OT_BACKBONE_ROUTER_MULTICAST_LISTENER_ADDED)
    ListenerAdded = OT_BACKBONE_ROUTER_MULTICAST_LISTENER_ADDED as isize,

    /// Functional equivalent of [`otsys::OT_BACKBONE_ROUTER_MULTICAST_LISTENER_REMOVED`](crate::otsys::OT_BACKBONE_ROUTER_MULTICAST_LISTENER_REMOVED)
    ListenerRemoved = OT_BACKBONE_ROUTER_MULTICAST_LISTENER_REMOVED as isize,
}

impl From<otBackboneRouterMulticastListenerEvent> for BackboneRouterMulticastListenerEvent {
    fn from(x: otBackboneRouterMulticastListenerEvent) -> Self {
        use num::FromPrimitive;
        Self::from_u32(x).unwrap_or_else(|| {
            panic!("Unknown otBackboneRouterMulticastListenerEvent value: {}", x)
        })
    }
}

impl From<BackboneRouterMulticastListenerEvent> for otBackboneRouterMulticastListenerEvent {
    fn from(x: BackboneRouterMulticastListenerEvent) -> Self {
        x as otBackboneRouterMulticastListenerEvent
    }
}
