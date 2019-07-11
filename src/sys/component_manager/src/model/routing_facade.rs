// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        directory_broker::RoutingFn,
        model::{moniker::AbsoluteMoniker, routing::*, Model},
    },
    cm_rust::{Capability, UseDecl},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_async as fasync,
    log::*,
};

/// A facade over `Model` that provides factories for routing functions.
#[derive(Clone)]
pub struct RoutingFacade {
    model: Model,
}

impl RoutingFacade {
    pub fn new(model: Model) -> Self {
        RoutingFacade { model }
    }

    /// Returns a factory for functions that route a `use` declaration to its source.
    pub fn route_use_fn_factory(&self) -> impl Fn(AbsoluteMoniker, UseDecl) -> RoutingFn {
        let model = self.model.clone();
        move |abs_moniker: AbsoluteMoniker, use_: UseDecl| {
            let model = model.clone();
            route_use_fn(model, abs_moniker, use_)
        }
    }

    /// Returns a factory for functions that route an `expose` declaration to its source.
    pub fn route_expose_fn_factory(&self) -> impl Fn(AbsoluteMoniker, Capability) -> RoutingFn {
        let model = self.model.clone();
        move |abs_moniker: AbsoluteMoniker, capability: Capability| {
            let model = model.clone();
            route_expose_fn(model, abs_moniker, capability)
        }
    }
}

fn route_use_fn(model: Model, abs_moniker: AbsoluteMoniker, use_: UseDecl) -> RoutingFn {
    Box::new(
        move |_flags: u32, mode: u32, _relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let model = model.clone();
            let abs_moniker = abs_moniker.clone();
            let use_ = use_.clone();
            fasync::spawn(async move {
                let res = await!(route_use_capability(
                    &model,
                    mode,
                    &use_,
                    abs_moniker.clone(),
                    server_end.into_channel()
                ));
                if let Err(e) = res {
                    error!("failed to route service for exposed dir {}: {:?}", abs_moniker, e);
                }
            });
        },
    )
}

fn route_expose_fn(
    model: Model,
    abs_moniker: AbsoluteMoniker,
    capability: Capability,
) -> RoutingFn {
    Box::new(
        move |_flags: u32, mode: u32, _relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let model = model.clone();
            let abs_moniker = abs_moniker.clone();
            let capability = capability.clone();
            fasync::spawn(async move {
                let res = await!(route_expose_capability(
                    &model,
                    mode,
                    &capability,
                    abs_moniker.clone(),
                    server_end.into_channel(),
                ));
                if let Err(e) = res {
                    eprintln!("failed to route service for exposed dir {}: {:?}", abs_moniker, e);
                }
            });
        },
    )
}
