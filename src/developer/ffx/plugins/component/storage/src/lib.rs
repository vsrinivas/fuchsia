// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_debug::storage::{copy, delete, list, make_directory},
    errors::ffx_error,
    ffx_component::rcs::connect_to_lifecycle_controller,
    ffx_component_storage_args::{StorageCommand, SubCommandEnum},
    ffx_core::ffx_plugin,
    fidl::endpoints::ServerEnd,
    fidl::handle::Channel,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_sys2::StorageAdminProxy,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    std::io::{stdout, Write},
};

#[ffx_plugin()]
pub async fn storage(remote_proxy: RemoteControlProxy, args: StorageCommand) -> Result<()> {
    let moniker = AbsoluteMoniker::parse_str(args.provider.as_str())
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?
        .to_string();

    let mut write = Box::new(stdout());
    let writer = &mut write;

    let lifecycle_controller = connect_to_lifecycle_controller(&remote_proxy).await?;
    let (client, server) = Channel::create()?;

    let server_end = ServerEnd::new(server);

    // LifecycleController accepts RelativeMonikers only
    let parent_moniker = format!(".{}", moniker.to_string());

    let res = lifecycle_controller
        .get_storage_admin(&parent_moniker, args.capability.as_str(), server_end)
        .await?;

    match res {
        Ok(_) => {
            let storage_admin =
                StorageAdminProxy::new(fidl::AsyncChannel::from_channel(client).unwrap());
            storage_cmd(storage_admin, args.subcommand).await
        }
        Err(e) => {
            writeln!(writer, "Failed to connect to service: {:?}", e)?;
            Ok(())
        }
    }
}

async fn storage_cmd(storage_admin: StorageAdminProxy, subcommand: SubCommandEnum) -> Result<()> {
    match subcommand {
        SubCommandEnum::Copy(args) => {
            copy(storage_admin, args.source_path, args.destination_path).await
        }
        SubCommandEnum::Delete(args) => delete(storage_admin, args.path).await,
        SubCommandEnum::List(args) => print_list(storage_admin, args.path).await,
        SubCommandEnum::MakeDirectory(args) => make_directory(storage_admin, args.path).await,
    }
}

pub async fn print_list(storage_admin: StorageAdminProxy, path: String) -> Result<()> {
    let entries = list(storage_admin, path).await?;

    let mut writer = std::io::stdout();
    for entry in entries {
        writeln!(writer, "{}", entry)?;
    }
    Ok(())
}
