// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! C bindings for wlansoftmac-rust crate.

// Explicitly declare usage for cbindgen.

use {
    tracing::error,
    wlan_mlme::{buffer::BufferProvider, device::DeviceInterface},
    wlan_span::CSpan,
    wlansoftmac_rust::{start_wlansoftmac, WlanSoftmacHandle},
};

#[no_mangle]
pub extern "C" fn start_sta(
    device: DeviceInterface,
    buf_provider: BufferProvider,
) -> *mut WlanSoftmacHandle {
    match start_wlansoftmac(device, buf_provider) {
        Ok(handle) => Box::into_raw(Box::new(handle)),
        Err(e) => {
            error!("Failed to start WLAN Softmac STA: {}", e);
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn stop_sta(softmac: &mut WlanSoftmacHandle) {
    softmac.stop();
}

/// FFI interface: Stop and delete a WlanSoftmac via the WlanSoftmacHandle.
/// Takes ownership and invalidates the passed WlanSoftmacHandle.
///
/// # Safety
///
/// This fn accepts a raw pointer that is held by the FFI caller as a handle to
/// the Softmac. This API is fundamentally unsafe, and relies on the caller to
/// pass the correct pointer and make no further calls on it later.
#[no_mangle]
pub unsafe extern "C" fn delete_sta(softmac: *mut WlanSoftmacHandle) {
    if !softmac.is_null() {
        let softmac = Box::from_raw(softmac);
        softmac.delete();
    }
}

#[no_mangle]
pub extern "C" fn sta_queue_eth_frame_tx(softmac: &mut WlanSoftmacHandle, frame: CSpan<'_>) {
    let _ = softmac.queue_eth_frame_tx(frame.into());
}
