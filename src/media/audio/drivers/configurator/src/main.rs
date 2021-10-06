// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use files_async;
use fuchsia_inspect::{component, health::Reporter};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use std::path::{Path, PathBuf};
use tracing;

const CODEC_DEVICE_DIR: &str = "/dev/class/codec";

/// Finds any CODEC devices, connects to any that are available
pub async fn find_devices() -> Result<Vec<PathBuf>, Error> {
    // Connect to the component's environment.
    let directory_proxy = open_directory_in_namespace(CODEC_DEVICE_DIR, OPEN_RIGHT_READABLE)?;
    let files = files_async::readdir(&directory_proxy).await?;
    let paths: Vec<_> =
        files.iter().map(|file| Path::new(CODEC_DEVICE_DIR).join(&file.name)).collect();

    Ok(paths)
}

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    component::health().set_ok();
    tracing::trace!("Initialized.");

    let devices_paths = find_devices().await?;
    tracing::info!("Devices paths: {:?}", devices_paths);
    Ok(())
}
