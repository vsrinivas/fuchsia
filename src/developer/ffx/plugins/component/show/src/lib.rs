// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    cs::{io::Directory, v2::V2Component, Subcommand},
    errors::ffx_error,
    ffx_component::COMPONENT_SHOW_HELP,
    ffx_component_show_args::ComponentShowCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
};

#[ffx_plugin()]
pub async fn show(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentShowCommand) -> Result<()> {
    show_impl(rcs_proxy, &cmd.filter).await
}

async fn show_impl(rcs_proxy: rc::RemoteControlProxy, filter: &str) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);
    let component = V2Component::explore(hub_dir, Subcommand::Show).await;
    component.print_details(&filter).map_err(|e| {
        ffx_error!(
            "'{}' was not found in the component tree: {}\n{}",
            filter,
            e,
            COMPONENT_SHOW_HELP
        )
    })?;
    Ok(())
}
