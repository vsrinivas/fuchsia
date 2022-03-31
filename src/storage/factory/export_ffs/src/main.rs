// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    argh::FromArgs,
    export_ffs::export_directory,
    fdio, fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
};

/// A command line tool for generating factoryfs partitions by flattening existing directory
/// structures.
#[derive(Debug, FromArgs)]
struct Args {
    /// path to the directory to flatten into a factoryfs partition.
    #[argh(positional)]
    directory: String,

    /// block device to write the factoryfs partition to. THIS IS DESTRUCTIVE!
    #[argh(positional)]
    device: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();

    // open directory
    let (dir_proxy_chan, dir_proxy_server_end) = zx::Channel::create()?;
    fdio::open(
        &args.directory,
        fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_DIRECTORY,
        dir_proxy_server_end,
    )
    .context(format!("failed to open '{}'", &args.directory))?;
    let dir_proxy = fio::DirectoryProxy::new(fasync::Channel::from_channel(dir_proxy_chan)?);

    // open block device
    let (device, device_server_end) = zx::Channel::create()?;
    fdio::service_connect(&args.device, device_server_end)
        .context(format!("failed to open block device '{}'", &args.device))?;

    // call the exporter
    export_directory(&dir_proxy, device).await.context(format!(
        "failed to export '{}' to block device '{}'",
        &args.directory, &args.device
    ))?;

    Ok(())
}
