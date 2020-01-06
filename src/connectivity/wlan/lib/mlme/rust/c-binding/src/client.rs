// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    banjo_ddk_protocol_wlan_info as banjo_wlan_info, banjo_ddk_protocol_wlan_mac as banjo_wlan_mac,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::error,
    wlan_common::TimeUnit,
    wlan_mlme::{
        buffer::BufferProvider,
        client::{temporary_c_binding::*, Client, ClientConfig, ClientMlme},
        common::{
            mac::{self, Bssid, PowerState},
            sequence::SequenceManager,
        },
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

#[no_mangle]
pub extern "C" fn client_sta_new(
    bssid: &[u8; 6],
    iface_mac: &[u8; 6],
    is_rsn: bool,
) -> *mut Client {
    Box::into_raw(Box::new(Client::new(Bssid(*bssid), *iface_mac, is_rsn)))
}

#[no_mangle]
pub extern "C" fn client_sta_delete(sta: *mut Client) {
    if !sta.is_null() {
        unsafe { Box::from_raw(sta) };
    }
}

/// Return true if auto-deauth triggers. Return false otherwise
#[no_mangle]
pub extern "C" fn client_mlme_timeout_fired(
    mlme: &mut ClientMlme,
    sta: *mut Client,
    event_id: EventId,
) -> bool {
    let sta = if !sta.is_null() { unsafe { Some(&mut *sta) } } else { None };
    mlme.handle_timed_event(sta, event_id)
}

#[no_mangle]
pub extern "C" fn client_mlme_seq_mgr(mlme: &mut ClientMlme) -> &mut SequenceManager {
    mlme.seq_mgr()
}

#[no_mangle]
pub extern "C" fn client_mlme_handle_mlme_msg(
    mlme: &mut ClientMlme,
    sta: *mut Client,
    bytes: CSpan<'_>,
) -> i32 {
    let sta = if !sta.is_null() { unsafe { Some(&mut *sta) } } else { None };
    #[allow(deprecated)] // Allow until main message loop is in Rust.
    match fidl_mlme::MlmeRequestMessage::decode(bytes.into(), &mut []) {
        Ok(msg) => mlme.handle_mlme_msg(sta, msg).into_raw_zx_status(),
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
pub extern "C" fn client_mlme_set_main_channel(
    mlme: &mut ClientMlme,
    main_channel: banjo_wlan_info::WlanChannel,
) -> i32 {
    let result = mlme.set_main_channel(main_channel);
    if result.is_ok() {
        zx::sys::ZX_OK
    } else {
        result.unwrap_err().into_raw()
    }
}

#[no_mangle]
pub extern "C" fn client_mlme_on_mac_frame(
    mlme: &mut ClientMlme,
    sta: *mut Client,
    frame: CSpan<'_>,
    rx_info: *const banjo_wlan_mac::WlanRxInfo,
) {
    let sta = if !sta.is_null() { unsafe { Some(&mut *sta) } } else { None };
    let rx_info = if !rx_info.is_null() { unsafe { Some((&*rx_info).clone()) } } else { None };
    mlme.on_mac_frame(sta, frame.into(), rx_info);
}

#[no_mangle]
pub extern "C" fn client_mlme_hw_scan_complete(mlme: &mut ClientMlme, status: u8) {
    mlme.handle_hw_scan_complete(banjo_wlan_mac::WlanHwScan(status));
}

#[no_mangle]
pub extern "C" fn client_sta_ensure_on_channel(sta: &mut Client, mlme: &mut ClientMlme) {
    c_ensure_on_channel(sta, mlme)
}

#[no_mangle]
pub extern "C" fn client_sta_start_lost_bss_counter(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    beacon_period: u16,
) {
    c_start_lost_bss_counter(sta, mlme, TimeUnit(beacon_period));
}

#[no_mangle]
pub extern "C" fn client_sta_stop_lost_bss_counter(sta: &mut Client) {
    sta.stop_lost_bss_counter();
}

#[no_mangle]
pub extern "C" fn client_sta_send_open_auth_frame(sta: &mut Client, mlme: &mut ClientMlme) -> i32 {
    c_send_open_auth_frame(sta, mlme).into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_send_deauth_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    reason_code: u16,
) -> i32 {
    c_send_deauth_frame(sta, mlme, mac::ReasonCode(reason_code)).into_raw_zx_status()
}

#[no_mangle]
pub extern "C" fn client_sta_send_assoc_req_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    cap_info: u16,
    ssid: CSpan<'_>,
    rates: CSpan<'_>,
    rsne: CSpan<'_>,
    ht_cap: CSpan<'_>,
    vht_cap: CSpan<'_>,
) -> i32 {
    c_send_assoc_req_frame(
        sta,
        mlme,
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
    mlme: &mut ClientMlme,
    data_frame: CSpan<'_>,
    has_padding: bool,
    controlled_port_open: bool,
) -> i32 {
    // TODO(42080): Do not parse here. Instead, parse in associated state only.
    match mac::MacFrame::parse(data_frame.into(), has_padding) {
        Some(mac::MacFrame::<&[u8]>::Data { fixed_fields, addr4, qos_ctrl, body, .. }) => {
            c_handle_data_frame(
                sta,
                mlme,
                &fixed_fields,
                addr4.map(|x| *x),
                qos_ctrl.map(|x| x.get()),
                body,
                controlled_port_open,
            );
        }
        // Silently discard corrupted frame.
        _ => (),
    };
    zx::sys::ZX_OK
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_data_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    src: &[u8; 6],
    dest: &[u8; 6],
    is_protected: bool,
    is_qos: bool,
    ether_type: u16,
    payload: CSpan<'_>,
) -> i32 {
    c_send_data_frame(sta, mlme, *src, *dest, is_protected, is_qos, ether_type, payload.into())
        .into_raw_zx_status()
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_handle_eth_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    frame: CSpan<'_>,
) -> i32 {
    c_on_eth_frame(sta, mlme, frame.into()).into_raw_zx_status()
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_eapol_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    src: &[u8; 6],
    dest: &[u8; 6],
    is_protected: bool,
    payload: CSpan<'_>,
) {
    c_send_eapol_frame(sta, mlme, *src, *dest, is_protected, payload.into())
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_ps_poll_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    aid: u16,
) -> i32 {
    c_send_ps_poll_frame(sta, mlme, aid).into_raw_zx_status()
}

#[no_mangle]
pub unsafe extern "C" fn client_sta_send_power_state_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    state: PowerState,
) -> i32 {
    c_send_power_state_frame(sta, mlme, state).into_raw_zx_status()
}
