// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    component_hub::{
        io::Directory,
        show::{find_components, Component},
    },
    ffx_component::COMPONENT_SHOW_HELP,
    ffx_component_show_args::ComponentShowCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
};

/// The number of times the command should be retried before assuming failure.
const NUM_ATTEMPTS: u64 = 3;

#[ffx_plugin()]
pub async fn show(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentShowCommand) -> Result<()> {
    show_impl(rcs_proxy, &cmd.filter).await
}

// Attempt to get matching components `NUM_ATTEMPTS` times. If all attempts fail, return the
// last error encountered.
//
// This fixes an issue (fxbug.dev/84805) where the component topology may be mutating while the
// hub is being traversed, resulting in failures.
pub async fn try_get_components(query: String, hub_dir: Directory) -> Result<Vec<Component>> {
    let mut attempt_number = 1;
    loop {
        match find_components(query.clone(), hub_dir.clone()).await {
            Ok(components) => return Ok(components),
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

async fn show_impl(rcs_proxy: rc::RemoteControlProxy, filter: &str) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);

    let components = try_get_components(filter.to_string(), hub_dir).await?;

    if components.is_empty() {
        return Err(format_err!(
            "'{}' was not found in the component tree\n{}",
            filter,
            COMPONENT_SHOW_HELP
        ));
    }

    for component in components {
        println!("{}", component);
    }
    Ok(())
}
