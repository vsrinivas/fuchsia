// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::FxFilesystem, anyhow::Error, std::sync::Arc, storage_device::DeviceHolder,
};

pub async fn mount(device: DeviceHolder) -> Result<Arc<FxFilesystem>, Error> {
    let fs = FxFilesystem::open(device).await?;
    Ok(fs)
}

pub async fn mount_read_only(device: DeviceHolder) -> Result<Arc<FxFilesystem>, Error> {
    let fs = FxFilesystem::open_read_only(device).await?;
    Ok(fs)
}
