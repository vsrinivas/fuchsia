// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod fshost;
mod storage;

use {
    argh::{from_env, FromArgs},
    tracing::info,
};

const BLOBFS_MOUNTPOINT: &str = "/blobfs";

/// Flags for fshost.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to fvm block file used to bootstrap blobfs.
    #[argh(option)]
    fvm_block_file_path: String,
}

#[fuchsia::main]
async fn main() {
    info!("Starting fshost...");

    let args: Args = from_env();

    info!(?args, "Initalizing fshost");

    fshost::FSHost::new(&args.fvm_block_file_path, BLOBFS_MOUNTPOINT).await.serve().await
}
