// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::error,
    wlan_mlme::{
        buffer::BufferProvider,
        client::Client,
        common::{
            mac::{self, Bssid},
            sequence::SequenceManager,
        },
        device::Device,
        error::ResultExt,
        timer::*,
    },
    wlan_span::CSpan,
};

#[no_mangle]
pub extern "C" fn client_sta_new(
    device: Device,
    buf_provider: BufferProvider,
    scheduler: Scheduler,
    bssid: &[u8; 6],
    iface_mac: &[u8; 6],
) -> *mut Client {
    Box::into_raw(Box::new(Client::new(device, buf_provider, scheduler, Bssid(*bssid), *iface_mac)))
}

#[no_mangle]
pub extern "C" fn client_sta_delete(sta: *mut Client) {
    if !sta.is_null() {
        unsafe { Box::from_raw(sta) };
    }
}

#[no_mangle]
pub extern "C" fn client_sta_timeout_fired(sta: &mut Client, event_id: EventId) {
    sta.handle_timed_event(event_id);
}

#[no_mangle]
pub extern "C" fn client_sta_seq_mgr(sta: &mut Client) -> &mut SequenceManager {
    sta.seq_mgr()
}

#[no_mangle]
pub extern "C" fn client_sta_handle_mlme_msg(sta: &mut Client, bytes: CSpan<'_>) -> i32 {
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
pub extern "C" fn client_sta_send_open_auth_frame(sta: &mut Client) -> i32 {
    sta.send_open_auth_frame().into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_send_deauth_frame(sta: &mut Client, reason_code: u16) -> i32 {
    sta.send_deauth_frame(mac::ReasonCode(reason_code)).into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_send_assoc_req_frame(
    sta: &mut Client,
    cap_info: u16,
    ssid: CSpan<'_>,
    rates: CSpan<'_>,
    rsne: CSpan<'_>,
    ht_cap: CSpan<'_>,
    vht_cap: CSpan<'_>,
) -> i32 {
    sta.send_assoc_req_frame(
        cap_info,
        ssid.into(),
        rates.into(),
        rsne.into(),
        ht_cap.into(),
        vht_cap.into(),
    )
    .into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_handle_data_frame(
    sta: &mut Client,
    data_frame: CSpan<'_>,
    has_padding: bool,
    controlled_port_open: bool,
) -> i32 {
    // Safe here because |data_frame_slice| does not outlive |data_frame|.
    let data_frame_slice: &[u8] = data_frame.into();
    sta.handle_data_frame(data_frame_slice, has_padding, controlled_port_open);
    zx::sys::ZX_OK
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_data_frame(
    sta: &mut Client,
    src: &[u8; 6],
    dest: &[u8; 6],
    is_protected: bool,
    is_qos: bool,
    ether_type: u16,
    payload: CSpan<'_>,
) -> i32 {
    sta.send_data_frame(*src, *dest, is_protected, is_qos, ether_type, payload.into())
        .into_raw_zx_status()
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_eapol_frame(
    sta: &mut Client,
    src: &[u8; 6],
    dest: &[u8; 6],
    is_protected: bool,
    payload: CSpan<'_>,
) {
    sta.send_eapol_frame(*src, *dest, is_protected, payload.into())
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_ps_poll_frame(sta: &mut Client, aid: u16) -> i32 {
    sta.send_ps_poll_frame(aid).into_raw_zx_status()
}
