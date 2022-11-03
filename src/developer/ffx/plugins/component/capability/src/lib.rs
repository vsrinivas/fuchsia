// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_debug::capability::{
        find_instances_that_expose_or_use_capability, MatchingInstances,
    },
    ffx_component::{connect_to_realm_explorer, connect_to_realm_query},
    ffx_component_capability_args::ComponentCapabilityCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc,
};

#[ffx_plugin()]
pub async fn capability_cmd(
    rcs_proxy: rc::RemoteControlProxy,
    cmd: ComponentCapabilityCommand,
) -> Result<()> {
    let query_proxy = connect_to_realm_query(&rcs_proxy).await?;
    let explorer_proxy = connect_to_realm_explorer(&rcs_proxy).await?;

    let MatchingInstances { exposed, used } =
        find_instances_that_expose_or_use_capability(cmd.capability, &explorer_proxy, &query_proxy)
            .await?;

    if !exposed.is_empty() {
        println!("Exposed:");
        for component in exposed {
            println!("  {}", component);
        }
    }
    if !used.is_empty() {
        println!("Used:");
        for component in used {
            println!("  {}", component);
        }
    }

    Ok(())
}
