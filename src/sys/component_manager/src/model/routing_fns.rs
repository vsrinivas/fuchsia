// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilitySource, ComponentCapability},
        model::{
            realm::WeakRealm,
            routing::{
                self, open_capability_at_source, route_expose_capability, route_use_capability,
            },
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

pub fn route_use_fn(realm: WeakRealm, use_: UseDecl) -> RoutingFn {
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
                let mut server_end = server_end.into_channel();
                let res = route_use_capability(
                    flags,
                    mode,
                    relative_path,
                    &use_,
                    &realm,
                    &mut server_end,
                )
                .await;
                if let Err(e) = res {
                    let cap = ComponentCapability::Use(use_);
                    routing::report_routing_failure(&realm.abs_moniker, &cap, &e, server_end);
                }
            });
        },
    )
}

// TODO(44746): Remove and use `route_use_fn_factory` instead
pub fn route_capability_source(realm: WeakRealm, source: CapabilitySource) -> RoutingFn {
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
                let mut server_end = server_end.into_channel();
                let res = open_capability_at_source(
                    flags,
                    mode,
                    PathBuf::from(relative_path),
                    source,
                    &realm,
                    &mut server_end,
                )
                .await;
                if let Err(_) = res {
                    // Don't bother reporting an error in this case. This function is only used
                    // by the CapabilityUsageTree in the hub, and that should be using route_use_fn
                    // anyway.
                }
            });
        },
    )
}

pub fn route_expose_fn(realm: WeakRealm, expose: ExposeDecl) -> RoutingFn {
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
                let mut server_end = server_end.into_channel();
                let res = route_expose_capability(
                    flags,
                    mode,
                    relative_path,
                    &expose,
                    &realm,
                    &mut server_end,
                )
                .await;
                if let Err(e) = res {
                    let cap = ComponentCapability::UsedExpose(expose);
                    routing::report_routing_failure(&realm.abs_moniker, &cap, &e, server_end);
                }
            });
        },
    )
}
