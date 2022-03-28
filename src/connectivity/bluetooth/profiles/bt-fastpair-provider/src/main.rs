// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use tracing::info;

mod gatt_service;

use gatt_service::GattService;

#[fuchsia::component(logging_tags = ["bt-fastpair-provider"])]
async fn main() -> Result<(), Error> {
    info!("Fast Pair Provider component running.");

    // TODO(fxbug.dev/95542): Publish the GATT service using the `GattService` object and plumb to
    // the rest of the component.
    let _gatt_service = GattService::new()?;
    Ok(())
}
