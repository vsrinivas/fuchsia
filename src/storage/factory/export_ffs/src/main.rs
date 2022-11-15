// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    argh::FromArgs,
    export_ffs::export_directory,
    fidl::endpoints::{ClientEnd, Proxy as _},
    fidl_fuchsia_hardware_block::{BlockMarker, BlockProxy},
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
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
    let Args { directory, device } = argh::from_env();

    // open directory
    let dir = fuchsia_fs::directory::open_in_namespace(&directory, fio::OpenFlags::RIGHT_READABLE)?;

    // open block device
    let proxy = fuchsia_component::client::connect_to_protocol_at_path::<BlockMarker>(&device)
        .with_context(|| format!("failed to open {}", &device))?;
    let channel = proxy
        .into_channel()
        .map_err(|_: BlockProxy| anyhow::anyhow!("failed to get block channel"))?;
    let client_end = ClientEnd::<BlockMarker>::new(channel.into());

    // call the exporter
    let () = export_directory(&dir, client_end).await.with_context(|| {
        format!("failed to export '{}' to block device '{}'", &directory, &device)
    })?;

    Ok(())
}
