// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{device::Device, object_store::FxFilesystem},
    anyhow::Error,
    std::sync::Arc,
};

pub async fn mount(device: Arc<dyn Device>) -> Result<Arc<FxFilesystem>, Error> {
    let fs = FxFilesystem::open(device).await?;
    Ok(fs)
}
