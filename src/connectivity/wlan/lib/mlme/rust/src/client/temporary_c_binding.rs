// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Functions used by C-binding crate. They are created here because we need access to fields
//! inside ClientMlme and would rather not make them public

use {
    crate::{
        client::{send_power_state_frame, Client, ClientMlme},
        error::Error,
    },
    wlan_common::{
        mac::{self, Aid, MacAddr, PowerState},
        TimeUnit,
    },
    zerocopy::ByteSlice,
};

pub fn c_ensure_on_channel(sta: &mut Client, mlme: &mut ClientMlme) {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .ensure_on_channel();
}

pub fn c_start_lost_bss_counter(sta: &mut Client, mlme: &mut ClientMlme, beacon_period: TimeUnit) {
    sta.start_lost_bss_counter(&mut mlme.ctx, beacon_period);
}

pub fn c_reset_lost_bss_timeout(sta: &mut Client, mlme: &mut ClientMlme) {
    sta.reset_lost_bss_timeout(&mut mlme.ctx);
}

pub fn c_send_open_auth_frame(sta: &mut Client, mlme: &mut ClientMlme) -> Result<(), Error> {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .send_open_auth_frame()
}

pub fn c_send_deauth_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    reason_code: mac::ReasonCode,
) -> Result<(), Error> {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .send_deauth_frame(reason_code)
}

pub fn c_send_assoc_req_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    cap_info: u16,
    ssid: &[u8],
    rates: &[u8],
    rsne: &[u8],
    ht_cap: &[u8],
    vht_cap: &[u8],
) -> Result<(), Error> {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .send_assoc_req_frame(cap_info, ssid, rates, rsne, ht_cap, vht_cap)
}

pub fn c_handle_data_frame<B: ByteSlice>(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    fixed_data_fields: &mac::FixedDataHdrFields,
    addr4: Option<mac::Addr4>,
    qos_ctrl: Option<mac::QosControl>,
    body: B,
    is_controlled_port_open: bool,
) {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .handle_data_frame(fixed_data_fields, addr4, qos_ctrl, body, is_controlled_port_open)
}

pub fn c_send_data_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    src: MacAddr,
    dst: MacAddr,
    is_protected: bool,
    is_qos: bool,
    ether_type: u16,
    payload: &[u8],
) -> Result<(), Error> {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .send_data_frame(src, dst, is_protected, is_qos, ether_type, payload)
}

pub fn c_on_eth_frame(sta: &mut Client, mlme: &mut ClientMlme, frame: &[u8]) -> Result<(), Error> {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .on_eth_frame(frame)
}

pub fn c_send_eapol_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    src: MacAddr,
    dst: MacAddr,
    is_protected: bool,
    eapol_frame: &[u8],
) {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .send_eapol_frame(src, dst, is_protected, eapol_frame)
}

pub fn c_send_ps_poll_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    aid: Aid,
) -> Result<(), Error> {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .send_ps_poll_frame(aid)
}

pub fn c_send_power_state_frame(
    sta: &mut Client,
    mlme: &mut ClientMlme,
    state: PowerState,
) -> Result<(), Error> {
    send_power_state_frame(sta, &mut mlme.ctx, state)
}

pub fn c_send_addba_req_frame(sta: &mut Client, mlme: &mut ClientMlme) -> Result<(), Error> {
    sta.bind(&mut mlme.ctx, &mut mlme.scanner, &mut mlme.chan_sched, &mut mlme.channel_state)
        .send_addba_req_frame()
}
