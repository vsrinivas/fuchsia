// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    wlan_common::mac::Bssid,
    wlan_mlme::{ap::Ap, buffer::BufferProvider, common::mac, device::Device, error::ResultExt},
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
pub extern "C" fn ap_sta_send_open_auth_frame(
    sta: &mut Ap,
    client_addr: &[u8; 6],
    status_code: u16,
) -> i32 {
    sta.send_open_auth_frame(*client_addr, mac::StatusCode(status_code)).into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn ap_sta_delete(sta: *mut Ap) {
    if !sta.is_null() {
        unsafe { Box::from_raw(sta) };
    }
}
