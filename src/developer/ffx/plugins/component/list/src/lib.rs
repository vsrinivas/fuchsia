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

/// The number of times the command should be retried before assuming failure.
const NUM_ATTEMPTS: u64 = 3;

#[ffx_plugin()]
pub async fn list(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentListCommand) -> Result<()> {
    list_impl(rcs_proxy, cmd.only, cmd.verbose).await
}

// Attempt to get the component list `NUM_ATTEMPTS` times. If all attempts fail, return the
// last error encountered.
//
// This fixes an issue (fxbug.dev/84805) where the component topology may be mutating while the
// hub is being traversed, resulting in failures.
pub async fn try_get_component_list(hub_dir: Directory) -> Result<Component> {
    let mut attempt_number = 1;
    loop {
        match Component::parse("/".to_string(), hub_dir.clone()).await {
            Ok(component) => return Ok(component),
            Err(e) => {
                if attempt_number > NUM_ATTEMPTS {
                    return Err(e);
                } else {
                    eprintln!("Retrying. Attempt #{} failed: {}", attempt_number, e);
                }
            }
        }
        attempt_number += 1;
    }
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

    let component = try_get_component_list(hub_dir).await?;

    if let Some(list_filter) = list_filter {
        component.print(&list_filter, verbose, 0);
    } else {
        // Default option is printing all components
        component.print(&ListFilter::None, verbose, 0);
    }
    Ok(())
}
