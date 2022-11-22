// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, component_debug::copy::copy, errors::ffx_bail,
    ffx_component::rcs::connect_to_realm_query, ffx_component_copy_args::CopyComponentCommand,
    ffx_core::ffx_plugin, fidl_fuchsia_developer_remotecontrol as rc,
};

#[ffx_plugin]
pub async fn component_copy(
    rcs_proxy: rc::RemoteControlProxy,
    cmd: CopyComponentCommand,
) -> Result<()> {
    let query_proxy = connect_to_realm_query(&rcs_proxy).await?;
    let CopyComponentCommand { paths } = cmd;

    match copy(&query_proxy, paths).await {
        Ok(_) => Ok(()),
        Err(e) => ffx_bail!("{}", e),
    }
}
