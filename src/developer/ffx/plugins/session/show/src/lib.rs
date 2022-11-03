// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    component_debug::show::find_instances,
    errors::ffx_error,
    ffx_component_show::create_table,
    ffx_core::ffx_plugin,
    ffx_session_show_args::SessionShowCommand,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon_status::Status,
};

const DETAILS_FAILURE: &str = "Could not get session information from the target. This may be
because there are no running sessions, or because the target is using a product configuration
that does not support the session framework.";

#[ffx_plugin()]
async fn show(rcs_proxy: rc::RemoteControlProxy, _cmd: SessionShowCommand) -> Result<()> {
    let (explorer_proxy, explorer_server) =
        fidl::endpoints::create_proxy::<fsys::RealmExplorerMarker>()
            .context("creating explorer proxy")?;
    let (query_proxy, query_server) = fidl::endpoints::create_proxy::<fsys::RealmQueryMarker>()
        .context("creating query proxy")?;
    rcs_proxy
        .root_realm_explorer(explorer_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening realm explorer")?;
    rcs_proxy
        .root_realm_query(query_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening realm query")?;

    let instances = find_instances("session:session".to_string(), &explorer_proxy, &query_proxy)
        .await
        .map_err(|e| ffx_error!("Error: {}. {}", e, DETAILS_FAILURE))?;

    if instances.is_empty() {
        return Err(format_err!(
            "No instances found matching filter `session:session`. {}",
            DETAILS_FAILURE
        ));
    }

    for instance in instances {
        let table = create_table(instance);
        table.printstd();
        println!("");
    }

    Ok(())
}
