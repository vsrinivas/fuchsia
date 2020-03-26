// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilitySource, ComponentCapability},
        model::{
            realm::WeakRealm,
            routing::{open_capability_at_source, route_expose_capability, route_use_capability},
        },
    },
    cm_rust::{ExposeDecl, UseDecl},
    directory_broker::RoutingFn,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_async as fasync,
    log::*,
    std::path::PathBuf,
};

/// A facade over `Model` that provides factories for routing functions.
#[derive(Clone)]
pub struct RoutingFacade;

impl RoutingFacade {
    pub fn new() -> Self {
        RoutingFacade {}
    }

    /// Returns a factory for functions that route a `use` declaration to its source.
    pub fn route_use_fn_factory(&self) -> impl Fn(WeakRealm, UseDecl) -> RoutingFn {
        move |realm: WeakRealm, use_: UseDecl| route_use_fn(realm, use_)
    }

    /// Returns a factory for functions that route an `expose` declaration to its source.
    pub fn route_expose_fn_factory(&self) -> impl Fn(WeakRealm, ExposeDecl) -> RoutingFn {
        move |realm: WeakRealm, expose: ExposeDecl| route_expose_fn(realm, expose)
    }

    /// Returns a factory for functions that route a component to a capability source.
    pub fn route_capability_source_fn_factory(
        &self,
    ) -> impl Fn(WeakRealm, CapabilitySource) -> RoutingFn {
        move |realm: WeakRealm, source: CapabilitySource| route_capability_source(realm, source)
    }
}

fn route_use_fn(realm: WeakRealm, use_: UseDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let realm = realm.clone();
            let use_ = use_.clone();
            fasync::spawn(async move {
                let realm = match realm.upgrade() {
                    Ok(realm) => realm,
                    Err(e) => {
                        // This can happen if the component instance tree topology changes such
                        // that the captured `realm` no longer exists.
                        error!(
                            "failed to upgrade WeakRealm while routing use decl `{:?}`: {:?}",
                            &use_, e
                        );
                        return;
                    }
                };
                let res = route_use_capability(
                    flags,
                    mode,
                    relative_path,
                    &use_,
                    &realm,
                    server_end.into_channel(),
                )
                .await;
                if let Err(e) = res {
                    error!(
                        "failed to route service from use decl `{:?}` for exposed dir {}: {:?}",
                        &use_, &realm.abs_moniker, e
                    )
                }
            });
        },
    )
}

fn route_capability_source(realm: WeakRealm, source: CapabilitySource) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let realm = realm.clone();
            let source = source.clone();
            fasync::spawn(async move {
                let realm = match realm.upgrade() {
                    Ok(realm) => realm,
                    Err(e) => {
                        // This can happen if the component instance tree topology changes such
                        // that the captured `realm` no longer exists.
                        error!(
                            "failed to upgrade WeakRealm while opening `{:?}`: {:?}",
                            &source, e
                        );
                        return;
                    }
                };
                let res = open_capability_at_source(
                    flags,
                    mode,
                    PathBuf::from(relative_path),
                    source,
                    &realm,
                    server_end.into_channel(),
                )
                .await;
                if let Err(e) = res {
                    error!("failed to route capability for {}: {:?}", &realm.abs_moniker, e);
                }
            });
        },
    )
}

fn route_expose_fn(realm: WeakRealm, expose: ExposeDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let realm = realm.clone();
            let expose = expose.clone();
            fasync::spawn(async move {
                let realm = match realm.upgrade() {
                    Ok(realm) => realm,
                    Err(e) => {
                        // This can happen if the component instance tree topology changes such
                        // that the captured `realm` no longer exists.
                        error!(
                            "failed to upgrade WeakRealm while routing expose decl `{:?}`: {:?}",
                            &expose, e
                        );
                        return;
                    }
                };
                let res = route_expose_capability(
                    flags,
                    mode,
                    relative_path,
                    &expose,
                    &realm,
                    server_end.into_channel(),
                )
                .await;
                if let Err(e) = res {
                    let cap = ComponentCapability::UsedExpose(expose);
                    error!(
                        "Failed to route capability `{}` from component `{}`: {}",
                        cap.source_id(),
                        &realm.abs_moniker,
                        e
                    );
                }
            });
        },
    )
}
