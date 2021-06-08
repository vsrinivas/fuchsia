// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context, Error};
use argh::FromArgs;
use fidl_fuchsia_lowpan_device::RoutePreference;

/// Contains the arguments decoded for the `get-external-routes` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-external-routes")]
pub struct GetExternalRoutesCommand {}

impl GetExternalRoutesCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device_route = context.get_default_device_route_extra_proxy().await?;

        let external_routes = device_route
            .get_local_external_routes()
            .await
            .context("Unable to send get_local_external_routes command")?;

        if external_routes.is_empty() {
            println!("No local external routes.");
        }

        for prefix in external_routes {
            if prefix.subnet.is_none() {
                continue;
            }
            println!(
                "{}{}{}",
                prefix
                    .subnet
                    .map(|subnet| format!(
                        "{}/{}",
                        std::net::Ipv6Addr::from(subnet.addr.addr),
                        subnet.prefix_len
                    ))
                    .unwrap(),
                match prefix.route_preference {
                    Some(RoutePreference::High) => " PRI-HIGH",
                    Some(RoutePreference::Medium) => "",
                    Some(RoutePreference::Low) => " PRI-LOW",
                    None => "",
                },
                match prefix.stable {
                    Some(true) => " STABLE",
                    _ => "",
                },
            );
        }
        Ok(())
    }
}
