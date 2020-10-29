// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::ComponentCapability,
        model::{
            realm::{Runtime, WeakRealm},
            routing::{self, route_expose_capability, route_use_capability},
        },
    },
    cm_rust::{ExposeDecl, UseDecl},
    directory_broker::RoutingFn,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_async as fasync,
    log::*,
};

pub fn route_use_fn(realm: WeakRealm, use_: UseDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let realm = realm.clone();
            let use_ = use_.clone();
            fasync::Task::spawn(async move {
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
                    let execution = realm.lock_execution().await;
                    let logger = match &execution.runtime {
                        Some(Runtime { namespace: Some(ns), .. }) => Some(ns.get_logger()),
                        _ => None,
                    };
                    routing::report_routing_failure(
                        &realm.abs_moniker,
                        &cap,
                        &e,
                        server_end,
                        logger,
                    );
                }
            })
            .detach();
        },
    )
}

pub fn route_expose_fn(realm: WeakRealm, expose: ExposeDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let realm = realm.clone();
            let expose = expose.clone();
            fasync::Task::spawn(async move {
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
                    let execution = realm.lock_execution().await;
                    let logger = match &execution.runtime {
                        Some(Runtime { namespace: Some(ns), .. }) => Some(ns.get_logger()),
                        _ => None,
                    };
                    routing::report_routing_failure(
                        &realm.abs_moniker,
                        &cap,
                        &e,
                        server_end,
                        logger,
                    );
                }
            })
            .detach();
        },
    )
}
