// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        directory_broker::RoutingFn,
        model::{moniker::AbsoluteMoniker, routing::*, Model},
    },
    cm_rust::Capability,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_async as fasync,
    log::*,
    std::sync::Arc,
};

pub trait CapabilityRoutingFnFactory {
    fn create_route_fn(&self, abs_moniker: &AbsoluteMoniker, capability: Capability) -> RoutingFn;
}

pub type RoutingFnFactory = Arc<dyn CapabilityRoutingFnFactory + Send + Sync>;

pub struct ModelCapabilityRoutingFnFactory {
    pub model: Model,
}

impl ModelCapabilityRoutingFnFactory {
    pub fn new(model: &Model) -> Self {
        ModelCapabilityRoutingFnFactory { model: model.clone() }
    }
}

impl CapabilityRoutingFnFactory for ModelCapabilityRoutingFnFactory {
    fn create_route_fn(&self, abs_moniker: &AbsoluteMoniker, capability: Capability) -> RoutingFn {
        let abs_moniker = abs_moniker.clone();
        let model = self.model.clone();
        Box::new(
            move |_flags: u32,
                  _mode: u32,
                  _relative_path: String,
                  server_end: ServerEnd<NodeMarker>| {
                let model = model.clone();
                let abs_moniker = abs_moniker.clone();
                let capability = capability.clone();
                fasync::spawn(async move {
                    // `route_service` is used for directories as well. The directory capability
                    // is modeled as a service node of the containing directory.
                    // TODO(fsamuel): we might want to get rid of `route_directory` and
                    // `route_service`; instead, we can expose `route_use_capability` and have the
                    // caller be responsible for passing in the right mode.
                    let res = await!(route_service(
                        &model,
                        &capability,
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
}
