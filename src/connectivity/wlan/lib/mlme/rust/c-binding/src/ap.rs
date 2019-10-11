// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    wlan_common::mac::Bssid,
    wlan_mlme::{ap::Ap, buffer::BufferProvider, device::Device},
};

#[no_mangle]
pub extern "C" fn ap_sta_new(
    device: Device,
    buf_provider: BufferProvider,
    bssid: &[u8; 6],
) -> *mut Ap {
    Box::into_raw(Box::new(Ap::new(device, buf_provider, Bssid(*bssid))))
}

#[no_mangle]
pub extern "C" fn ap_sta_delete(sta: *mut Ap) {
    if !sta.is_null() {
        unsafe { Box::from_raw(sta) };
    }
}
