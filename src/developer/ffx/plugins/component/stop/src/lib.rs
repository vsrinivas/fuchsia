// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, format_err, Context, Result},
    component_hub::io::Directory,
    errors::{ffx_bail, ffx_error},
    ffx_component::{get_lifecycle_controller_proxy, parse_moniker, COMPONENT_STOP_HELP},
    ffx_component_stop_args::ComponentStopCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
};

#[ffx_plugin()]
pub async fn stop(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentStopCommand) -> Result<()> {
    stop_impl(rcs_proxy, &cmd.moniker, cmd.recursive, &mut std::io::stdout()).await
}

async fn stop_impl<W: std::io::Write>(
    rcs_proxy: rc::RemoteControlProxy,
    moniker: &str,
    recursive: bool,
    writer: &mut W,
) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;

    let formatted_moniker = parse_moniker(moniker).map_err(|e| {
        ffx_error!(
            "Failed to stop the component with moniker '{}': {}\n{}",
            moniker,
            e,
            COMPONENT_STOP_HELP
        )
    })?;

    let hub_dir = Directory::from_proxy(root);
    if !hub_dir.exists("debug").await? {
        ffx_bail!("Unable to find a 'debug' directory in the hub. This may mean you're using an old Fuchsia image. Please report this issue to the ffx team.")
    }

    if recursive {
        writeln!(writer, "Stopping the component with moniker '{}' recursively", moniker)?;
    } else {
        writeln!(writer, "Stopping the component with moniker '{}'", moniker)?;
    }

    match get_lifecycle_controller_proxy(hub_dir.proxy).await {
        Ok(proxy) => match proxy.stop(&formatted_moniker.to_string(), recursive).await {
            Ok(Ok(())) => {
                writeln!(writer, "Successfully stopped the component with moniker '{}'", moniker)?;
            }
            Ok(Err(e)) => {
                ffx_bail!(
                    "Failed to stop the component with moniker '{}': {:?}\n{}",
                    moniker,
                    e,
                    COMPONENT_STOP_HELP
                );
            }
            Err(e) => bail!("FIDL error: {}\n", e),
        },
        Err(e) => {
            Err(format_err!("Failed to stop the component with moniker '{}': {}\n", moniker, e)
                .context(format!("stopping the component with moniker '{}'", moniker)))?
        }
    }

    Ok(())
}
