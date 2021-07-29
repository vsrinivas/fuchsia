// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    component_hub::{
        io::Directory,
        list::{Component, ListFilter},
    },
    ffx_component_list_args::ComponentListCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
};

#[ffx_plugin()]
pub async fn list(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentListCommand) -> Result<()> {
    list_impl(rcs_proxy, cmd.only, cmd.verbose).await
}

async fn list_impl(
    rcs_proxy: rc::RemoteControlProxy,
    list_filter: Option<ListFilter>,
    verbose: bool,
) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);

    let component = Component::parse(".".to_string(), hub_dir).await?;

    if let Some(list_filter) = list_filter {
        component.print(&list_filter, verbose, 0);
    } else {
        // Default option is printing all components
        component.print(&ListFilter::None, verbose, 0);
    }
    Ok(())
}
