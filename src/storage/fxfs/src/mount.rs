// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{
        crypt::Crypt,
        filesystem::{FxFilesystem, OpenFxFilesystem, OpenOptions},
    },
    anyhow::Error,
    std::sync::Arc,
    storage_device::DeviceHolder,
};

pub async fn mount(device: DeviceHolder, crypt: Arc<dyn Crypt>) -> Result<OpenFxFilesystem, Error> {
    let fs = FxFilesystem::open(device, crypt).await?;
    Ok(fs)
}

pub async fn mount_with_options(
    device: DeviceHolder,
    options: OpenOptions,
    crypt: Arc<dyn Crypt>,
) -> Result<OpenFxFilesystem, Error> {
    let fs = FxFilesystem::open_with_options(device, options, crypt).await?;
    Ok(fs)
}
