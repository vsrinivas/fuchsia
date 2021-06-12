// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error, Result},
    cs::io::Directory,
    errors::{ffx_bail, ffx_error},
    ffx_component::COMPONENT_BIND_HELP,
    ffx_component_bind_args::ComponentBindCommand,
    ffx_core::ffx_plugin,
    fidl::endpoints::{Proxy, ServiceMarker},
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon_status::Status,
    moniker::RelativeMoniker,
    std::path::PathBuf,
};

#[ffx_plugin()]
pub async fn bind(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentBindCommand) -> Result<()> {
    bind_impl(rcs_proxy, &cmd.moniker).await
}

async fn bind_impl(rcs_proxy: rc::RemoteControlProxy, moniker: &str) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;

    let mut formatted_moniker = String::from("./");
    formatted_moniker.push_str(&moniker);

    let formatted_moniker = RelativeMoniker::parse_string_without_instances(&formatted_moniker)
        .map_err(|e| {
            ffx_error!(
                "Failed to bind to the component with moniker '{}': {}\n{}",
                moniker,
                e,
                COMPONENT_BIND_HELP
            )
        })?;
    // TODO(fxbug.dev/77451): remove the unsupported error once LifecycleController supports instanceless monikers.
    if formatted_moniker.up_path().len() > 0 {
        ffx_bail!(
            "Failed to bind to the component with moniker '{}': {}\n{}",
            moniker,
            "monikers with non-empty up_path are not supported",
            COMPONENT_BIND_HELP
        );
    }
    for child_moniker in formatted_moniker.down_path() {
        if child_moniker.collection().is_some() {
            ffx_bail!(
                "Failed to bind to the component with moniker '{}': {}\n{}",
                moniker,
                "monikers for instances in collections are not supported",
                COMPONENT_BIND_HELP
            );
        }
    }

    let hub_dir = Directory::from_proxy(root);
    if hub_dir.exists("debug").await {
        match get_lifecycle_controller_proxy(hub_dir.proxy).await {
            Ok(proxy) => match proxy.bind(&formatted_moniker.to_string()).await {
                Ok(Ok(())) => {
                    println!("Successfully bound to the component with moniker '{}'", moniker);
                }
                Ok(Err(e)) => {
                    ffx_bail!(
                        "Failed to bind to the component with moniker '{}': {:?}\n{}",
                        moniker,
                        e,
                        COMPONENT_BIND_HELP
                    );
                }
                Err(e) => Err(format_err!("FIDL error: {}\n", e))?,
            },
            Err(e) => Err(format_err!(
                "Failed to bind to the component with moniker '{}': {}\n",
                moniker,
                e
            )
            .context(format!("binding to the component with moniker '{}'", moniker)))?,
        }
    } else {
        ffx_bail!("Unable to find a 'debug' directory in the hub. This may mean you're using an old Fuchsia image. Please report this issue to the ffx team.")
    };
    Ok(())
}

async fn get_lifecycle_controller_proxy(
    root: fio::DirectoryProxy,
) -> Result<fsys::LifecycleControllerProxy, Error> {
    let lifecycle_controller_path =
        PathBuf::from("debug").join(fsys::LifecycleControllerMarker::NAME);
    match io_util::open_node(
        &root,
        lifecycle_controller_path.as_path(),
        fio::OPEN_RIGHT_READABLE,
        0,
    ) {
        Ok(node_proxy) => Ok(fsys::LifecycleControllerProxy::from_channel(
            node_proxy.into_channel().expect("could not get channel from proxy"),
        )),
        Err(e) => Err(format_err!("could not open node proxy: {}", e)),
    }
}
