// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    banjo_ddk_protocol_wlan_mac as banjo_wlan_mac, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon as zx,
    log::error,
    wlan_mlme::{
        buffer::BufferProvider,
        client::{ClientConfig, ClientMlme},
        device::Device,
        error::ResultExt,
        timer::*,
    },
    wlan_span::CSpan,
};

#[no_mangle]
pub extern "C" fn client_mlme_new(
    config: ClientConfig,
    device: Device,
    buf_provider: BufferProvider,
    scheduler: Scheduler,
) -> *mut ClientMlme {
    Box::into_raw(Box::new(ClientMlme::new(config, device, buf_provider, scheduler)))
}
#[no_mangle]
pub extern "C" fn client_mlme_delete(mlme: *mut ClientMlme) {
    if !mlme.is_null() {
        unsafe { Box::from_raw(mlme) };
    }
}

/// Return true if auto-deauth triggers. Return false otherwise
#[no_mangle]
pub extern "C" fn client_mlme_timeout_fired(mlme: &mut ClientMlme, event_id: EventId) {
    mlme.handle_timed_event(event_id)
}

#[no_mangle]
pub extern "C" fn client_mlme_handle_mlme_msg(mlme: &mut ClientMlme, bytes: CSpan<'_>) -> i32 {
    #[allow(deprecated)] // Allow until main message loop is in Rust.
    match fidl_mlme::MlmeRequestMessage::decode(bytes.into(), &mut []) {
        Ok(msg) => mlme.handle_mlme_msg(msg).into_raw_zx_status(),
        Err(e) => {
            error!("error decoding MLME message: {}", e);
            zx::Status::IO.into_raw()
        }
    }
}

#[no_mangle]
pub extern "C" fn client_mlme_on_channel(mlme: &mut ClientMlme) -> bool {
    mlme.on_channel()
}

#[no_mangle]
pub extern "C" fn client_mlme_on_mac_frame(
    mlme: &mut ClientMlme,
    bytes: CSpan<'_>,
    rx_info: *const banjo_wlan_mac::WlanRxInfo,
) {
    // unsafe is ok because we checked rx_info is not a nullptr.
    let rx_info = if !rx_info.is_null() { Some(unsafe { *rx_info }) } else { None };
    mlme.on_mac_frame(bytes.into(), rx_info);
}

#[no_mangle]
pub extern "C" fn client_mlme_hw_scan_complete(mlme: &mut ClientMlme, status: u8) {
    mlme.handle_hw_scan_complete(banjo_wlan_mac::WlanHwScan(status));
}

#[no_mangle]
pub unsafe extern "C" fn client_mlme_handle_eth_frame(
    mlme: &mut ClientMlme,
    frame: CSpan<'_>,
) -> i32 {
    mlme.on_eth_frame::<&[u8]>(frame.into()).into_raw_zx_status()
}
