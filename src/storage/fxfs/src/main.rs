// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fuchsia_async as fasync,
    fuchsia_component::server::MissingStartupHandle,
    fuchsia_runtime::HandleType,
    fuchsia_syslog, fuchsia_zircon as zx,
    fxfs::{device::block_device::BlockDevice, mkfs, mount, server::FxfsServer},
    remote_block_device::RemoteBlockClient,
    std::sync::Arc,
};

#[derive(FromArgs, PartialEq, Debug)]
/// fxfs
struct TopLevel {
    #[argh(subcommand)]
    nested: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    Format(FormatSubCommand),
    Mount(MountSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Format
#[argh(subcommand, name = "mkfs")]
struct FormatSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// Mount
#[argh(subcommand, name = "mount")]
struct MountSubCommand {}

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();

    let args: TopLevel = argh::from_env();

    let client = RemoteBlockClient::new(zx::Channel::from(
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
            HandleType::User0,
            1,
        ))
        .ok_or(format_err!("Missing device handle"))?,
    ))?;
    let device = Arc::new(BlockDevice::new(Box::new(client)));

    match args {
        TopLevel { nested: SubCommand::Format(_) } => {
            mkfs::mkfs(device).await?;
            Ok(())
        }
        TopLevel { nested: SubCommand::Mount(_) } => {
            let fs = mount::mount(device).await?;
            let mut server = FxfsServer::new(fs, "default").await?;
            let startup_handle =
                fuchsia_runtime::take_startup_handle(HandleType::DirectoryRequest.into())
                    .ok_or(MissingStartupHandle)?;
            server.run(zx::Channel::from(startup_handle)).await
        }
    }
}
