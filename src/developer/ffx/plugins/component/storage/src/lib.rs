// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    ffx_component::storage::{copy, list, make_directory},
    ffx_component_storage_args::{Provider, StorageCommand, SubcommandEnum},
    ffx_core::ffx_plugin,
    fidl::handle::Channel,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_sys2::StorageAdminProxy,
    selectors::{self, VerboseError},
    std::io::{stdout, Write},
};

#[ffx_plugin()]
pub async fn storage(remote_proxy: RemoteControlProxy, args: StorageCommand) -> Result<()> {
    let mut write = Box::new(stdout());
    let writer = &mut write;
    let selector = selectors::parse_selector::<VerboseError>(match args.provider {
        Provider::Data => &"core:expose:fuchsia.sys2.StorageAdmin"[..],
        Provider::Cache => &"core:expose:fuchsia.sys2.StorageAdmin.cache"[..],
        Provider::Temp => &"core:expose:fuchsia.sys2.StorageAdmin.tmp"[..],
    })
    .unwrap();

    let (client, server) = Channel::create()?;

    match remote_proxy.connect(selector, server).await.context("awaiting connect call")? {
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

async fn storage_cmd(storage_admin: StorageAdminProxy, subcommand: SubcommandEnum) -> Result<()> {
    match subcommand {
        SubcommandEnum::Copy(args) => {
            copy(storage_admin, args.source_path, args.destination_path).await
        }
        SubcommandEnum::List(args) => print_list(storage_admin, args.path).await,
        SubcommandEnum::MakeDirectory(args) => make_directory(storage_admin, args.path).await,
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
