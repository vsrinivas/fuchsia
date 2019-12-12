// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{model::Model, moniker::AbsoluteMoniker, routing},
    cm_rust::{ExposeDecl, UseDecl},
    directory_broker::RoutingFn,
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
    pub fn route_expose_fn_factory(&self) -> impl Fn(AbsoluteMoniker, ExposeDecl) -> RoutingFn {
        let model = self.model.clone();
        move |abs_moniker: AbsoluteMoniker, expose: ExposeDecl| {
            let model = model.clone();
            route_expose_fn(model, abs_moniker, expose)
        }
    }
}

fn route_use_fn(model: Model, abs_moniker: AbsoluteMoniker, use_: UseDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let model = model.clone();
            let abs_moniker = abs_moniker.clone();
            let use_ = use_.clone();
            fasync::spawn(async move {
                let res = routing::route_use_capability(
                    &model,
                    flags,
                    mode,
                    relative_path,
                    &use_,
                    abs_moniker.clone(),
                    server_end.into_channel(),
                )
                .await;
                if let Err(e) = res {
                    error!("failed to route service for exposed dir {}: {:?}", abs_moniker, e);
                }
            });
        },
    )
}

fn route_expose_fn(model: Model, abs_moniker: AbsoluteMoniker, expose: ExposeDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, _relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let model = model.clone();
            let abs_moniker = abs_moniker.clone();
            let expose = expose.clone();
            fasync::spawn(async move {
                let res = routing::route_expose_capability(
                    &model,
                    flags,
                    mode,
                    &expose,
                    abs_moniker.clone(),
                    server_end.into_channel(),
                )
                .await;
                if let Err(e) = res {
                    error!("failed to route service for exposed dir {}: {:?}", abs_moniker, e);
                }
            });
        },
    )
}
