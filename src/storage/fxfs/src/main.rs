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
    fuchsia_syslog, fuchsia_zircon as zx,
    fxfs::{
        crypt::{Crypt, InsecureCrypt},
        mkfs, mount,
        object_store::{
            filesystem::OpenOptions,
            fsck::{self},
        },
        remote_crypt::RemoteCrypt,
        serialized_types::LATEST_VERSION,
        server::FxfsServer,
    },
    remote_block_device::RemoteBlockClient,
    std::sync::Arc,
    storage_device::{block_device::BlockDevice, DeviceHolder},
};

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
struct MountSubCommand {
    /// mount the device as read-only
    #[argh(switch)]
    readonly: bool,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Fsck
#[argh(subcommand, name = "fsck")]
struct FsckSubCommand {}

// The number of threads chosen here must exceed the number of concurrent system calls to paged VMOs
// that we allow since otherwise deadlocks are possible.  Search for CONCURRENT_SYSCALLS.
#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();

    #[cfg(feature = "tracing")]
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    log::info!("fxfs version {} started {:?}", LATEST_VERSION, std::env::args());

    let args: TopLevel = argh::from_env();

    let client = RemoteBlockClient::new(zx::Channel::from(
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
            HandleType::User0,
            1,
        ))
        .ok_or(format_err!("Missing device handle"))?,
    ))
    .await?;

    let crypt: Arc<dyn Crypt> = match fuchsia_runtime::take_startup_handle(
        fuchsia_runtime::HandleInfo::new(HandleType::User0, 2),
    ) {
        Some(handle) => Arc::new(RemoteCrypt::new(CryptProxy::new(fasync::Channel::from_channel(
            zx::Channel::from(handle),
        )?))),
        None => {
            // TODO(csuter): We should find a way to ensure that InsecureCrypt isn't compiled in
            // production builds, and make this case throw an error.
            log::info!(
                "fxfs: Remote `Crypt` is `InsecureCrypt`! In a production build, this is a bug."
            );
            Arc::new(InsecureCrypt::new())
        }
    };

    match args {
        TopLevel { nested: SubCommand::Format(_), .. } => {
            mkfs::mkfs(DeviceHolder::new(BlockDevice::new(Box::new(client), false).await?), crypt)
                .await?;
            Ok(())
        }
        TopLevel { nested: SubCommand::Mount(MountSubCommand { readonly }), verbose } => {
            let fs = mount::mount_with_options(
                DeviceHolder::new(BlockDevice::new(Box::new(client), readonly).await?),
                OpenOptions { trace: verbose, read_only: readonly },
            )
            .await?;
            let server = FxfsServer::new(fs, "default", crypt).await?;
            let startup_handle =
                fuchsia_runtime::take_startup_handle(HandleType::DirectoryRequest.into())
                    .ok_or(MissingStartupHandle)?;
            server.run(zx::Channel::from(startup_handle)).await
        }
        TopLevel { nested: SubCommand::Fsck(_), verbose } => {
            let fs = mount::mount_with_options(
                DeviceHolder::new(BlockDevice::new(Box::new(client), true).await?),
                OpenOptions { read_only: true, trace: verbose, ..Default::default() },
            )
            .await?;
            let mut options = fsck::default_options();
            options.verbose = verbose;
            fsck::fsck_with_options(&fs, Some(crypt), options).await
        }
    }
}
