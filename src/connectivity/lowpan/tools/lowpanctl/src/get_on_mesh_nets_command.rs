// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context, Error};
use argh::FromArgs;
use fidl_fuchsia_lowpan_device::RoutePreference;

/// Contains the arguments decoded for the `get-on-mesh-nets` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-on-mesh-nets")]
pub struct GetOnMeshNetsCommand {}

impl GetOnMeshNetsCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device_route = context.get_default_device_route_extra_proxy().await?;

        let on_mesh_prefixes = device_route
            .get_local_on_mesh_prefixes()
            .await
            .context("Unable to send get_local_on_mesh_prefixes command")?;

        if on_mesh_prefixes.is_empty() {
            println!("No local on-mesh networks.");
        }

        for prefix in on_mesh_prefixes {
            if prefix.subnet.is_none() {
                continue;
            }
            println!(
                "{}{}{}{}{}",
                prefix
                    .subnet
                    .map(|subnet| format!(
                        "{}/{}",
                        std::net::Ipv6Addr::from(subnet.addr.addr),
                        subnet.prefix_len
                    ))
                    .unwrap(),
                match prefix.default_route_preference {
                    Some(RoutePreference::High) => " DEFAULT-HIGH",
                    Some(RoutePreference::Medium) => " DEFAULT",
                    Some(RoutePreference::Low) => " DEFAULT-LOW",
                    None => "",
                },
                match prefix.stable {
                    Some(true) => " STABLE",
                    _ => "",
                },
                match prefix.slaac_valid {
                    Some(true) => " VALID",
                    _ => "",
                },
                match prefix.slaac_preferred {
                    Some(true) => " PREFERRED",
                    _ => "",
                },
            );
        }
        Ok(())
    }
}
