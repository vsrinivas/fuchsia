// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ieee80211::Bssid,
    log::error,
    wlan_mlme::{ap::Ap, buffer::BufferProvider, device::DeviceInterface, Mlme, MlmeHandle},
    wlan_span::CSpan,
};

#[no_mangle]
pub extern "C" fn start_ap_sta(
    device: DeviceInterface,
    buf_provider: BufferProvider,
    bssid: &[u8; 6],
) -> *mut MlmeHandle {
    match Mlme::<Ap>::start(Bssid(*bssid), device, buf_provider) {
        Ok(ap_mlme) => Box::into_raw(Box::new(ap_mlme)),
        Err(e) => {
            error!("Failed to start AP MLME: {}", e);
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn start_ap_sta_for_test(
    device: DeviceInterface,
    buf_provider: BufferProvider,
    bssid: &[u8; 6],
) -> *mut MlmeHandle {
    Box::into_raw(Box::new(Mlme::<Ap>::start_test(Bssid(*bssid), device, buf_provider)))
}

#[no_mangle]
pub extern "C" fn stop_ap_sta(sta: &mut MlmeHandle) {
    sta.stop();
}

#[no_mangle]
pub extern "C" fn delete_ap_sta(sta: *mut MlmeHandle) {
    if !sta.is_null() {
        let mlme = unsafe { Box::from_raw(sta) };
        mlme.delete();
    }
}

#[no_mangle]
pub extern "C" fn ap_sta_queue_eth_frame_tx(sta: &mut MlmeHandle, frame: CSpan<'_>) {
    let _ = sta.queue_eth_frame_tx(frame.into());
}

#[no_mangle]
pub unsafe extern "C" fn ap_mlme_advance_fake_time(ap: &mut MlmeHandle, nanos: i64) {
    ap.advance_fake_time(nanos);
}

#[no_mangle]
pub unsafe extern "C" fn ap_mlme_run_until_stalled(sta: &mut MlmeHandle) {
    sta.run_until_stalled();
}
