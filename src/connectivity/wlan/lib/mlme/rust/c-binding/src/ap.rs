// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::error,
    wlan_common::mac::Bssid,
    wlan_mlme::{ap::Ap, buffer::BufferProvider, device::Device, error::ResultExt, timer::*},
    wlan_span::CSpan,
};

#[no_mangle]
pub extern "C" fn ap_sta_new(
    device: Device,
    buf_provider: BufferProvider,
    scheduler: Scheduler,
    bssid: &[u8; 6],
) -> *mut Ap {
    Box::into_raw(Box::new(Ap::new(device, buf_provider, scheduler, Bssid(*bssid))))
}

#[no_mangle]
pub extern "C" fn ap_sta_delete(sta: *mut Ap) {
    if !sta.is_null() {
        unsafe { Box::from_raw(sta) };
    }
}

#[no_mangle]
pub extern "C" fn ap_sta_timeout_fired(sta: &mut Ap, event_id: EventId) {
    sta.handle_timed_event(event_id);
}

#[no_mangle]
pub extern "C" fn ap_sta_handle_mlme_msg(sta: &mut Ap, bytes: CSpan<'_>) -> i32 {
    #[allow(deprecated)] // Allow until main message loop is in Rust.
    match fidl_mlme::MlmeRequestMessage::decode(bytes.into(), &mut []) {
        Ok(msg) => sta.handle_mlme_msg(msg).into_raw_zx_status(),
        Err(e) => {
            error!("error decoding MLME message: {}", e);
            zx::Status::IO.into_raw()
        }
    }
}

#[no_mangle]
pub extern "C" fn ap_sta_handle_mac_frame(
    sta: &mut Ap,
    frame: CSpan<'_>,
    body_aligned: bool,
) -> i32 {
    sta.handle_mac_frame::<&[u8]>(frame.into(), body_aligned);
    zx::sys::ZX_OK
}

#[no_mangle]
pub extern "C" fn ap_sta_handle_eth_frame(
    sta: &mut Ap,
    dst_addr: &[u8; 6],
    src_addr: &[u8; 6],
    ether_type: u16,
    body: CSpan<'_>,
) -> i32 {
    sta.handle_eth_frame(*dst_addr, *src_addr, ether_type, body.into());
    zx::sys::ZX_OK
}
