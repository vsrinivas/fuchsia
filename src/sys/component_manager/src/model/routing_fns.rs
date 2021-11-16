// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::WeakComponentInstance,
        routing::{
            self, route_and_open_namespace_capability,
            route_and_open_namespace_capability_from_expose,
        },
    },
    ::routing::capability_source::ComponentCapability,
    cm_rust::{ExposeDecl, UseDecl},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    log::*,
    vfs::{execution_scope::ExecutionScope, path::Path, remote::RoutingFn},
};

pub fn route_use_fn(component: WeakComponentInstance, use_: UseDecl) -> RoutingFn {
    Box::new(
        move |scope: ExecutionScope,
              flags: u32,
              mode: u32,
              path: Path,
              server_end: ServerEnd<NodeMarker>| {
            let component = component.clone();
            let use_ = use_.clone();
            scope.spawn(async move {
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
                    path.into_string(),
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
            });
        },
    )
}

pub fn route_expose_fn(component: WeakComponentInstance, expose: ExposeDecl) -> RoutingFn {
    Box::new(
        move |scope: ExecutionScope,
              flags: u32,
              mode: u32,
              path: Path,
              server_end: ServerEnd<NodeMarker>| {
            let component = component.clone();
            let expose = expose.clone();
            scope.spawn(async move {
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
                    path.into_string(),
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
            });
        },
    )
}
