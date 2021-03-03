// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    cs::{io::Directory, v2::V2Component, IncludeDetails},
    ffx_component_list_args::ComponentListCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
};

#[ffx_plugin()]
pub async fn list(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentListCommand) -> Result<()> {
    list_impl(rcs_proxy, cmd).await
}

async fn list_impl(rcs_proxy: rc::RemoteControlProxy, _cmd: ComponentListCommand) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);
    let component = V2Component::explore(hub_dir, IncludeDetails::No).await;
    component.print_tree();
    Ok(())
}
