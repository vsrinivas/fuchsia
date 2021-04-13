// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{device::Device, object_store::FxFilesystem, volume::root_volume},
    anyhow::Error,
    std::sync::Arc,
};

pub async fn mkfs(device: Arc<dyn Device>) -> Result<(), Error> {
    let fs = FxFilesystem::new_empty(device).await?;
    {
        // expect instead of propagating errors here, since otherwise we could drop |fs| before
        // close is called, which leads to confusing and unrelated error messages.
        let root_volume = root_volume(&fs).await.expect("Open root_volume failed");
        root_volume.new_volume("default").await.expect("Create volume failed");
    }
    fs.close().await?;
    Ok(())
}
