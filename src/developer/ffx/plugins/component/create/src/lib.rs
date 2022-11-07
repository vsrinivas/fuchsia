// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_error,
    ffx_component::connect_to_lifecycle_controller,
    ffx_component_create_args::CreateComponentCommand,
    ffx_component_create_lib::{create_component, IfExists, LIFECYCLE_ERROR_HELP},
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
};

#[ffx_plugin]
pub async fn create(rcs_proxy: rc::RemoteControlProxy, cmd: CreateComponentCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;
    let moniker = AbsoluteMoniker::parse_str(&cmd.moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;

    create_component(
        &lifecycle_controller,
        &moniker,
        &cmd.url,
        IfExists::Error(format!(
            "Component instances can be destroyed with the `ffx component destroy` command.\n{}",
            LIFECYCLE_ERROR_HELP,
        )),
        &mut std::io::stdout(),
    )
    .await
}
