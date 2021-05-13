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
    fxfs::{mkfs, mount, object_store::fsck::fsck, server::FxfsServer},
    remote_block_device::RemoteBlockClient,
    storage_device::{block_device::BlockDevice, DeviceHolder},
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
    Fsck(FsckSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Format
#[argh(subcommand, name = "mkfs")]
struct FormatSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// Mount
#[argh(subcommand, name = "mount")]
struct MountSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// Fsck
#[argh(subcommand, name = "fsck")]
struct FsckSubCommand {}

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
    ))
    .await?;

    match args {
        TopLevel { nested: SubCommand::Format(_) } => {
            mkfs::mkfs(DeviceHolder::new(BlockDevice::new(Box::new(client), false).await?)).await?;
            Ok(())
        }
        TopLevel { nested: SubCommand::Mount(_) } => {
            let fs =
                mount::mount(DeviceHolder::new(BlockDevice::new(Box::new(client), false).await?))
                    .await?;
            let server = FxfsServer::new(fs, "default").await?;
            let startup_handle =
                fuchsia_runtime::take_startup_handle(HandleType::DirectoryRequest.into())
                    .ok_or(MissingStartupHandle)?;
            server.run(zx::Channel::from(startup_handle)).await
        }
        TopLevel { nested: SubCommand::Fsck(_) } => {
            let fs =
                mount::mount(DeviceHolder::new(BlockDevice::new(Box::new(client), true).await?))
                    .await?;
            fsck(fs.as_ref()).await
        }
    }
}
