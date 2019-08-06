// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils,
    fuchsia_zircon as zx,
    num_traits::FromPrimitive,
    wlan_common::buffer_writer::BufferWriter,
    wlan_mlme::{
        buffer::{BufferProvider, InBuf, OutBuf},
        client::{self, ClientStation},
        common::{
            frame_len,
            mac::{self, OptionalField},
            sequence::SequenceManager,
        },
        device::{self, Device},
        error::ResultExt,
    },
};

#[no_mangle]
pub extern "C" fn client_sta_new(
    device: Device,
    buf_provider: BufferProvider,
    bssid: &[u8; 6],
    iface_mac: &[u8; 6],
) -> *mut ClientStation {
    Box::into_raw(Box::new(ClientStation::new(device, buf_provider, *bssid, *iface_mac)))
}

#[no_mangle]
pub extern "C" fn client_sta_delete(sta: *mut ClientStation) {
    if !sta.is_null() {
        unsafe { Box::from_raw(sta) };
    }
}

#[no_mangle]
pub extern "C" fn client_sta_seq_mgr(sta: &mut ClientStation) -> &mut SequenceManager {
    sta.seq_mgr()
}

#[no_mangle]
pub extern "C" fn client_sta_send_open_auth_frame(sta: &mut ClientStation) -> i32 {
    sta.send_open_auth_frame().into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_send_deauth_frame(sta: &mut ClientStation, reason_code: u16) -> i32 {
    sta.send_deauth_frame(mac::ReasonCode(reason_code)).into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_send_keep_alive_resp_frame(sta: &mut ClientStation) -> i32 {
    sta.send_keep_alive_resp_frame().into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_handle_data_frame(
    sta: &mut ClientStation,
    data_frame: *const u8,
    data_frame_len: usize,
    has_padding: bool,
) -> i32 {
    // Safe here because |data_frame_slice| does not outlive |data_frame|.
    let data_frame_slice = unsafe { utils::as_slice(data_frame, data_frame_len) };
    sta.handle_data_frame(data_frame_slice, has_padding);
    zx::sys::ZX_OK
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_data_frame(
    sta: &mut ClientStation,
    src: &[u8; 6],
    dest: &[u8; 6],
    is_protected: bool,
    is_qos: bool,
    ether_type: u16,
    payload: *const u8,
    payload_len: usize,
) -> i32 {
    let payload = utils::as_slice(payload, payload_len);
    sta.send_data_frame(*src, *dest, is_protected, is_qos, ether_type, payload).into_raw_zx_status()
}
