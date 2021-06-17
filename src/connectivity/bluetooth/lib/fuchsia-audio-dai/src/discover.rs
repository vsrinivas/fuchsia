// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use files_async;
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use std::path::Path;

use crate::DigitalAudioInterface;

const DAI_DEVICE_DIR: &str = "/dev/class/dai";

/// Finds any DAI devices, connects to any that are available and provides
pub async fn find_devices() -> Result<Vec<DigitalAudioInterface>, Error> {
    let directory_proxy = open_directory_in_namespace(DAI_DEVICE_DIR, OPEN_RIGHT_READABLE)?;

    let files = files_async::readdir(&directory_proxy).await?;

    let paths: Vec<_> =
        files.iter().map(|file| Path::new(DAI_DEVICE_DIR).join(&file.name)).collect();

    let devices = paths.iter().map(|path| DigitalAudioInterface::new(&path)).collect();

    Ok(devices)
}

#[cfg(test)]
mod tests {
    use fuchsia;

    use super::*;

    #[fuchsia::test]
    async fn test_env_dir_is_not_found() {
        let _ = find_devices().await.expect_err("find devices okay");
    }
}
