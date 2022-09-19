// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_hub::{
        select::find_instances_that_expose_or_use_capability, select::MatchingInstances,
    },
    ffx_component::{connect_to_realm_explorer, connect_to_realm_query},
    ffx_component_select_args::{CapabilityStruct, ComponentSelectCommand, SubCommandEnum},
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc,
};

#[ffx_plugin()]
pub async fn select_cmd(
    remote_proxy: rc::RemoteControlProxy,
    cmd: ComponentSelectCommand,
) -> Result<()> {
    match &cmd.nested {
        SubCommandEnum::Capability(CapabilityStruct { capability: c }) => {
            select_capability(remote_proxy, c).await
        }
    }
}

async fn select_capability(rcs_proxy: rc::RemoteControlProxy, capability: &str) -> Result<()> {
    let query_proxy = connect_to_realm_query(&rcs_proxy).await?;
    let explorer_proxy = connect_to_realm_explorer(&rcs_proxy).await?;

    let MatchingInstances { exposed, used } = find_instances_that_expose_or_use_capability(
        capability.to_string(),
        &explorer_proxy,
        &query_proxy,
    )
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
