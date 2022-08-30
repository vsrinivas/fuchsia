// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_tpm::{CommandRequest, TpmDeviceProxy};
use futures::lock::MutexGuard;

pub async fn handle_command_request(
    request: CommandRequest,
    device: MutexGuard<'_, TpmDeviceProxy>,
) -> Result<(), Error> {
    match request {
        CommandRequest::Transmit { data, responder } => {
            let mut device_response = device.execute_command(&data).await?;
            responder.send(&mut device_response).context("Sending transmit response")?;
        }
    }
    Ok(())
}
