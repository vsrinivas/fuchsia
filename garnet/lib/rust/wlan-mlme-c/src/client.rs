// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::buffer::{BufferProvider, InBuf, OutBuf},
    crate::utils,
    fuchsia_zircon::sys as zx,
    wlan_mlme::{
        client,
        common::mac::{self, OptionalField},
    },
};

#[no_mangle]
pub extern "C" fn rust_mlme_write_open_auth_frame(
    provider: BufferProvider,
    bssid: &[u8; 6],
    client_addr: &[u8; 6],
    seq_ctrl: u16,
    out_buf: &mut OutBuf,
) -> i32 {
    let frame_len = mac::MgmtHdr::len(mac::HtControl::ABSENT);
    let buf_result = provider.take_buffer(frame_len);
    let mut buf = unwrap_or_bail!(buf_result, zx::ZX_ERR_NO_RESOURCES);
    let write_result = client::write_open_auth_frame(&mut buf[..], *bssid, *client_addr, seq_ctrl);
    let written_bytes = unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL);
    *out_buf = OutBuf::from(buf, written_bytes);
    zx::ZX_OK
}

#[no_mangle]
pub extern "C" fn rust_mlme_write_keep_alive_resp_frame(
    provider: BufferProvider,
    bssid: &[u8; 6],
    client_addr: &[u8; 6],
    seq_ctrl: u16,
    out_buf: &mut OutBuf,
) -> i32 {
    let frame_len =
        mac::DataHdr::len(mac::Addr4::ABSENT, mac::QosControl::ABSENT, mac::HtControl::ABSENT);
    let buf_result = provider.get_buffer(frame_len);
    let mut buf = unwrap_or_bail!(buf_result, zx::ZX_ERR_NO_RESOURCES);
    let write_result =
        client::write_keep_alive_resp_frame(&mut buf[..], *bssid, *client_addr, seq_ctrl);
    let written_bytes = unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL);
    *out_buf = OutBuf::from(buf, written_bytes);
    zx::ZX_OK
}
