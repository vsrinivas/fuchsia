// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_tpm::{DeprovisionRequest, TpmDeviceProxy};
use fuchsia_zircon as zx;
use futures::lock::MutexGuard;

pub async fn handle_deprovision_request(
    request: DeprovisionRequest,
    _device: MutexGuard<'_, TpmDeviceProxy>,
) -> Result<(), Error> {
    match request {
        DeprovisionRequest::RemoveOwnership { responder, .. } => {
            let mut response = Err(zx::Status::NOT_SUPPORTED.into_raw());
            responder.send(&mut response).context("Sending RemoveOwnership response")?;
        }
    }
    Ok(())
}
