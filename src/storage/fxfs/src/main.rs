// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl_fuchsia_fxfs::CryptProxy,
    fuchsia_async as fasync,
    fuchsia_component::server::MissingStartupHandle,
    fuchsia_runtime::HandleType,
    fuchsia_zircon as zx,
    fxfs::{
        filesystem::{mkfs_with_default, FxFilesystem, OpenOptions},
        fsck,
        log::*,
        platform::{
            component::{new_block_client, Component},
            RemoteCrypt,
        },
        serialized_types::LATEST_VERSION,
    },
    std::sync::Arc,
    storage_device::{block_device::BlockDevice, DeviceHolder},
};

// TODO(fxbug.dev/93066): All commands other than 'component' should be removed and we should figure
// out how to make them work with componentized, multi-volume Fxfs.  These commands create Fxfs
// instances in the legacy format with a 'default' volume.

#[derive(FromArgs, PartialEq, Debug)]
/// fxfs
struct TopLevel {
    #[argh(subcommand)]
    nested: SubCommand,

    /// enable additional logging
    #[argh(switch)]
    verbose: bool,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    Component(ComponentSubCommand),
    Format(FormatSubCommand),
    Fsck(FsckSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Format
#[argh(subcommand, name = "mkfs")]
struct FormatSubCommand {
    /// make the default volume encrypted (using supplied crypt service)
    #[argh(switch)]
    encrypted: bool,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Fsck
#[argh(subcommand, name = "fsck")]
struct FsckSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// Component
#[argh(subcommand, name = "component")]
struct ComponentSubCommand {}

fn get_crypt_client() -> Result<Arc<RemoteCrypt>, Error> {
    Ok(Arc::new(RemoteCrypt::new(CryptProxy::new(fasync::Channel::from_channel(
        zx::Channel::from(
            fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
                HandleType::User0,
                2,
            ))
            .ok_or(format_err!("Missing crypt service"))?,
        ),
    )?))))
}

#[fasync::run(6)]
async fn main() -> Result<(), Error> {
    diagnostics_log::init!();

    #[cfg(feature = "tracing")]
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    info!(version = %LATEST_VERSION, "Started");

    let args: TopLevel = argh::from_env();

    if let TopLevel { nested: SubCommand::Component(_), .. } = args {
        return Component::new()
            .run(
                fuchsia_runtime::take_startup_handle(HandleType::DirectoryRequest.into())
                    .ok_or(MissingStartupHandle)?
                    .into(),
                fuchsia_runtime::take_startup_handle(HandleType::Lifecycle.into())
                    .map(|h| h.into()),
            )
            .await;
    }

    let client = new_block_client(zx::Channel::from(
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
            HandleType::User0,
            1,
        ))
        .ok_or(format_err!("Missing device handle"))?,
    ))
    .await?;

    match args {
        TopLevel { nested: SubCommand::Format(FormatSubCommand { encrypted }), .. } => {
            mkfs_with_default(
                DeviceHolder::new(BlockDevice::new(Box::new(client), false).await?),
                if encrypted { Some(get_crypt_client()?) } else { None },
            )
            .await?;
            Ok(())
        }
        TopLevel { nested: SubCommand::Fsck(FsckSubCommand {}), verbose } => {
            let fs = FxFilesystem::open_with_options(
                DeviceHolder::new(BlockDevice::new(Box::new(client), true).await?),
                OpenOptions { trace: verbose, ..OpenOptions::read_only(true) },
            )
            .await?;
            let mut options = fsck::FsckOptions::default();
            options.verbose = verbose;
            fsck::fsck_with_options(fs.clone(), &options).await
        }
        TopLevel { nested: SubCommand::Component(_), .. } => unreachable!(),
    }
}
