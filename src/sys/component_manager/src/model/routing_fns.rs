// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::ComponentCapability,
        model::{
            component::WeakComponentInstance,
            routing::{
                self, route_and_open_namespace_capability,
                route_and_open_namespace_capability_from_expose,
            },
        },
    },
    cm_rust::{ExposeDecl, UseDecl},
    directory_broker::RoutingFn,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_async as fasync,
    log::*,
};

pub fn route_use_fn(component: WeakComponentInstance, use_: UseDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let component = component.clone();
            let use_ = use_.clone();
            fasync::Task::spawn(async move {
                let component = match component.upgrade() {
                    Ok(component) => component,
                    Err(e) => {
                        // This can happen if the component instance tree topology changes such
                        // that the captured `component` no longer exists.
                        error!(
                            "failed to upgrade WeakComponentInstance while routing use \
                            decl `{:?}`: {:?}",
                            &use_, e
                        );
                        return;
                    }
                };
                let mut server_end = server_end.into_channel();
                let res = route_and_open_namespace_capability(
                    flags,
                    mode,
                    relative_path,
                    use_.clone(),
                    &component,
                    &mut server_end,
                )
                .await;
                if let Err(e) = res {
                    routing::report_routing_failure(
                        &component,
                        &ComponentCapability::Use(use_),
                        &e,
                        server_end,
                    )
                    .await;
                }
            })
            .detach();
        },
    )
}

pub fn route_expose_fn(component: WeakComponentInstance, expose: ExposeDecl) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            let component = component.clone();
            let expose = expose.clone();
            fasync::Task::spawn(async move {
                let component = match component.upgrade() {
                    Ok(component) => component,
                    Err(e) => {
                        // This can happen if the component instance tree topology changes such
                        // that the captured `component` no longer exists.
                        error!(
                            "failed to upgrade WeakComponentInstance while routing expose \
                            decl `{:?}`: {:?}",
                            &expose, e
                        );
                        return;
                    }
                };
                let mut server_end = server_end.into_channel();
                let res = route_and_open_namespace_capability_from_expose(
                    flags,
                    mode,
                    relative_path,
                    expose.clone(),
                    &component,
                    &mut server_end,
                )
                .await;
                if let Err(e) = res {
                    routing::report_routing_failure(
                        &component,
                        &ComponentCapability::Expose(expose),
                        &e,
                        server_end,
                    )
                    .await;
                }
            })
            .detach();
        },
    )
}
