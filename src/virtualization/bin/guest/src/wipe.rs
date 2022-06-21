// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl_fuchsia_virtualization::LinuxManagerProxy,
    fuchsia_zircon_status as zx_status,
};

pub async fn handle_wipe(linux_manager: LinuxManagerProxy) -> Result<(), Error> {
    match linux_manager.wipe_data().await?.map_err(zx_status::Status::from_raw) {
        Err(zx_status::Status::BAD_STATE) => {
            Err(anyhow!("The VM has already started. Please reboot your (host) device and retry before starting the VM."))
        }
        Err(status) => Err(anyhow!("Failed to wipe data: {}", status)),
        Ok(()) => Ok(()),
    }
}
