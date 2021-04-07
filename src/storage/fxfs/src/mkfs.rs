// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{device::Device, object_store::FxFilesystem, volume::volume_directory},
    anyhow::Error,
    std::sync::Arc,
};

pub async fn mkfs(device: Arc<dyn Device>) -> Result<(), Error> {
    let fs = FxFilesystem::new_empty(device).await?;
    {
        // expect instead of propagating errors here, since otherwise we could drop |fs| before
        // close is called, which leads to confusing and unrelated error messages.
        let volume_directory = volume_directory(&fs).await.expect("Open volume_directory failed");
        volume_directory.new_volume("default").await.expect("Create volume failed");
    }
    fs.close().await?;
    Ok(())
}
