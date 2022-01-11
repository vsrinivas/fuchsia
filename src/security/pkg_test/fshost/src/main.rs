// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod fshost;
mod pkgfs;
mod storage;

use {
    argh::{from_env, FromArgs},
    fuchsia_syslog::{fx_log_info, init},
};

const BLOBFS_MOUNTPOINT: &str = "/blobfs";

/// Flags for fshost.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to fvm block file used to bootstrap blobfs.
    #[argh(option)]
    fvm_block_file_path: String,
    /// absolute path to system image package file for bootstrapping pkgfs.
    #[argh(option)]
    system_image_path: String,
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    init().unwrap();

    fx_log_info!("Starting fshost...");

    let args: Args = from_env();

    fx_log_info!("Initalizing fshost with {:#?}", args);

    fshost::FSHost::new(&args.fvm_block_file_path, BLOBFS_MOUNTPOINT, &args.system_image_path)
        .await
        .serve()
        .await
}
