// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::filesystem::{FxFilesystem, OpenFxFilesystem, OpenOptions},
    anyhow::Error,
    storage_device::DeviceHolder,
};

pub async fn mount(device: DeviceHolder) -> Result<OpenFxFilesystem, Error> {
    let fs = FxFilesystem::open(device).await?;
    Ok(fs)
}

pub async fn mount_with_options(
    device: DeviceHolder,
    options: OpenOptions,
) -> Result<OpenFxFilesystem, Error> {
    let fs = FxFilesystem::open_with_options(device, options).await?;
    Ok(fs)
}
