// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_tpm::TpmDeviceProxy;
use fuchsia_fs::OpenFlags;
use fuchsia_zircon as zx;
use std::path::Path;

use tracing;

const TPM_PATH: &str = "/dev/class/tpm";

/// Returns `true` if a particular `TPM` device is supported.
///
/// Note that not all TPM devices are supported. A particular `revision_id` or
/// `device_id` range may be vulnerable or buggy and in these cases Fuchsia should not
/// recognize the vulnerable hardware instead of attempting to support a hardware
/// cryptographic stack on top of a vulnerable chip.
fn is_supported_tpm20(_vendor_id: u16, _device_id: u16, _revision_id: u8) -> bool {
    // TODO(benwright): Add supported tpm2.0 vendors.
    false
}

/// Returns a `TpmDeviceProxy` if the provided device identifies as a TPM 2.0.
///
/// Otherwise returns `None`. Errors only occur if the device cannot be opened or does not respond.
async fn try_connect_tpm20(
    device: &fio::DirectoryProxy,
    device_name: &str,
) -> Result<Option<TpmDeviceProxy>, Error> {
    let node = fuchsia_fs::open_node(
        device,
        Path::new(device_name),
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
        0,
    )
    .with_context(|| format!("Opening {}", device_name))?;
    let tpm_device_proxy =
        TpmDeviceProxy::new(node.into_channel().map_err(|e| anyhow!("{:?}", e))?);
    let (vendor_id, device_id, revision_id) = tpm_device_proxy
        .get_device_id()
        .await
        .with_context(|| format!("Sending GetDeviceId request to {}", device_name))?
        .map_err(zx::Status::from_raw)
        .with_context(|| format!("Retrieving GetDeviceIdResponse from {}", device_name))?;

    if is_supported_tpm20(vendor_id, device_id, revision_id) {
        Ok(Some(tpm_device_proxy))
    } else {
        tracing::warn!(
            "TPM device not supported: vendor_id: {:x}, device_id: {:x}, revision_id: {:x}",
            vendor_id,
            device_id,
            revision_id
        );
        Ok(None)
    }
}

/// Attempts to find the TPM from the well known dev path.
///
/// This function will iterate through all the entries in the dev
/// path until it finds a valid TPM. For a TPM to be valid it must
/// be a TPM 2.0, TPM 1.2 devices will not be accepted.
pub async fn find_tpm20() -> Result<TpmDeviceProxy, Error> {
    let tpm_proxy = fuchsia_fs::directory::open_in_namespace(
        TPM_PATH,
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .with_context(|| format!("Opening {}", TPM_PATH))?;

    // If multiple devices exist we always go with the first one in the order we iterate
    // the directory.
    let devices = fuchsia_fs::directory::readdir(&tpm_proxy)
        .await
        .with_context(|| format!("Reading {}", TPM_PATH))?;
    for device in devices.iter() {
        match try_connect_tpm20(&tpm_proxy, &device.name).await {
            Ok(Some(proxy)) => return Ok(proxy),
            Ok(None) => {}
            Err(e) => {
                tracing::warn!(
                    "Failed to determine if {}/{} is a tpm20: {:?}",
                    TPM_PATH,
                    device.name,
                    e
                );
            }
        }
    }
    Err(anyhow!("No supported TPM 2.0 device found"))
}
