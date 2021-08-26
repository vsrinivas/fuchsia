// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    component_hub::{io::Directory, show::find_components},
    errors::ffx_error,
    ffx_core::ffx_plugin,
    ffx_session_show_args::SessionShowCommand,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
};

const DETAILS_FAILURE: &str = "\
Could not get session information from the target.

This may be because there are no running sessions, or because the target is
using a product configuration that does not support the session framework.";

#[ffx_plugin()]
async fn show(rcs_proxy: rc::RemoteControlProxy, _cmd: SessionShowCommand) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);
    let components = find_components("session:session".to_string(), hub_dir)
        .await
        .map_err(|e| ffx_error!("{}\n\nError was: {}", DETAILS_FAILURE, e))?;

    if components.is_empty() {
        return Err(format_err!(
            "{}\n\nNo components found matching filter `session:session`",
            DETAILS_FAILURE
        ));
    }

    for component in components {
        println!("{}", component);
    }
    Ok(())
}
