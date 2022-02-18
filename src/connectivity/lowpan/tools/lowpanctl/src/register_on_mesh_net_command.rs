// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;
use fidl_fuchsia_lowpan::Ipv6Subnet;
use fidl_fuchsia_lowpan_device::{OnMeshPrefix, RoutePreference};
use fidl_fuchsia_net::Ipv6Address;

/// Contains the arguments decoded for the `register-on-mesh-net` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "register-on-mesh-net")]
pub struct RegisterOnMeshNetCommand {
    /// ipv6 prefix (always a /64)
    #[argh(positional)]
    pub addr: std::net::Ipv6Addr,

    /// offers a default route
    #[argh(switch)]
    pub default: bool,

    /// true if route is expected to be available for a while
    #[argh(switch)]
    pub stable: bool,

    /// slaac valid flag
    #[argh(switch)]
    pub slaac_valid: bool,

    /// slaac preferred flag
    #[argh(switch)]
    pub slaac_preferred: bool,
}

impl RegisterOnMeshNetCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device_route = context.get_default_device_route_proxy().await?;
        let prefix_len = 64;
        let subnet = Ipv6Subnet { addr: Ipv6Address { addr: self.addr.octets() }, prefix_len };

        let default_route_preference =
            if self.default { Some(RoutePreference::Medium) } else { None };

        let on_mesh_prefix = OnMeshPrefix {
            subnet: Some(subnet),
            default_route_preference,
            stable: Some(self.stable),
            slaac_preferred: Some(self.slaac_preferred),
            slaac_valid: Some(self.slaac_valid),
            ..OnMeshPrefix::EMPTY
        };

        device_route
            .register_on_mesh_prefix(on_mesh_prefix)
            .await
            .context("Unable to send register_on_mesh_prefix command")
    }
}
