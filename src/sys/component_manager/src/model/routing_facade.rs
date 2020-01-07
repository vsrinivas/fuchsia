// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            model::Model,
            moniker::AbsoluteMoniker,
            routing::{open_capability_at_source, route_expose_capability, route_use_capability},
        },
    },
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

    /// Returns a factory for functions that route a component to a capability source.
    pub fn route_capability_source_fn_factory(
        &self,
    ) -> impl Fn(AbsoluteMoniker, CapabilitySource) -> RoutingFn {
        let model = self.model.clone();
        move |abs_moniker: AbsoluteMoniker, source: CapabilitySource| {
            let model = model.clone();
            route_capability_source(model, abs_moniker, source)
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
                let abs_moniker_clone = abs_moniker.clone();
                let res = async move {
                    let target_realm = model.look_up_realm(&abs_moniker_clone).await?;
                    route_use_capability(
                        &model,
                        flags,
                        mode,
                        relative_path,
                        &use_,
                        &target_realm,
                        server_end.into_channel(),
                    )
                    .await
                }
                .await;
                if let Err(e) = res {
                    error!("failed to route service for exposed dir {}: {:?}", abs_moniker, e);
                }
            });
        },
    )
}

fn route_capability_source(
    model: Model,
    abs_moniker: AbsoluteMoniker,
    source: CapabilitySource,
) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let model = model.clone();
            let abs_moniker = abs_moniker.clone();
            let source = source.clone();
            fasync::spawn(async move {
                let abs_moniker_clone = abs_moniker.clone();
                let res = async move {
                    let target_realm = model.look_up_realm(&abs_moniker_clone).await?;
                    open_capability_at_source(
                        &model,
                        flags,
                        mode,
                        relative_path,
                        source,
                        &target_realm,
                        server_end.into_channel(),
                    )
                    .await
                }
                .await;
                if let Err(e) = res {
                    error!("failed to route service for {}: {:?}", abs_moniker, e);
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
                let abs_moniker_clone = abs_moniker.clone();
                let res = async move {
                    let target_realm = model.look_up_realm(&abs_moniker_clone).await?;
                    route_expose_capability(
                        &model,
                        flags,
                        mode,
                        &expose,
                        &target_realm,
                        server_end.into_channel(),
                    )
                    .await
                }
                .await;
                if let Err(e) = res {
                    error!("failed to route service for exposed dir {}: {:?}", abs_moniker, e);
                }
            });
        },
    )
}
