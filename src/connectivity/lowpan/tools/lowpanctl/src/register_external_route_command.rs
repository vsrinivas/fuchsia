// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context, Error};
use argh::FromArgs;
use fidl_fuchsia_lowpan::Ipv6Subnet;
use fidl_fuchsia_lowpan_device::ExternalRoute;
use fidl_fuchsia_net::Ipv6Address;

/// Contains the arguments decoded for the `register-external-route` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "register-external-route")]
pub struct RegisterExternalRouteCommand {
    /// ipv6 prefix (always a /64)
    #[argh(positional)]
    pub addr: std::net::Ipv6Addr,

    /// true if route is expected to be available for a while
    #[argh(switch)]
    pub stable: bool,
}

impl RegisterExternalRouteCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device_route = context.get_default_device_route_proxy().await?;
        let prefix_len = 64;
        let subnet = Ipv6Subnet { addr: Ipv6Address { addr: self.addr.octets() }, prefix_len };

        let on_mesh_prefix = ExternalRoute {
            subnet: Some(subnet),
            stable: Some(self.stable),
            ..ExternalRoute::EMPTY
        };

        device_route
            .register_external_route(on_mesh_prefix)
            .await
            .context("Unable to send register_external_route command")
    }
}
