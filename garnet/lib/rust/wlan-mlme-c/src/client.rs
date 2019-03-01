// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils,
    fuchsia_zircon::sys as zx,
    wlan_mlme::{
        buffer::{BufferProvider, InBuf, OutBuf},
        client,
        common::{frame_len, mac, sequence::SequenceManager},
    },
};

#[no_mangle]
pub extern "C" fn mlme_write_open_auth_frame(
    provider: BufferProvider,
    seq_mgr: &mut SequenceManager,
    bssid: &[u8; 6],
    client_addr: &[u8; 6],
    out_buf: &mut OutBuf,
) -> i32 {
    let frame_len = frame_len!(mac::MgmtHdr, mac::AuthHdr);
    let buf_result = provider.get_buffer(frame_len);
    let mut buf = unwrap_or_bail!(buf_result, zx::ZX_ERR_NO_RESOURCES);
    let write_result = client::write_open_auth_frame(&mut buf[..], *bssid, *client_addr, seq_mgr);
    let written_bytes = unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL).written_bytes();
    *out_buf = OutBuf::from(buf, written_bytes);
    zx::ZX_OK
}

#[no_mangle]
pub extern "C" fn mlme_write_keep_alive_resp_frame(
    provider: BufferProvider,
    seq_mgr: &mut SequenceManager,
    bssid: &[u8; 6],
    client_addr: &[u8; 6],
    out_buf: &mut OutBuf,
) -> i32 {
    let frame_len = frame_len!(mac::DataHdr);
    let buf_result = provider.get_buffer(frame_len);
    let mut buf = unwrap_or_bail!(buf_result, zx::ZX_ERR_NO_RESOURCES);
    let write_result =
        client::write_keep_alive_resp_frame(&mut buf[..], *bssid, *client_addr, seq_mgr);
    let written_bytes = unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL).written_bytes();
    *out_buf = OutBuf::from(buf, written_bytes);
    zx::ZX_OK
}

#[no_mangle]
pub extern "C" fn mlme_write_eth_frame(
    provider: BufferProvider,
    dst_addr: &[u8; 6],
    src_addr: &[u8; 6],
    protocol_id: u16,
    payload: *const u8,
    payload_len: usize,
    out_buf: &mut OutBuf,
) -> i32 {
    let frame_len = frame_len!(mac::EthernetIIHdr) + payload_len;
    let buf_result = provider.get_buffer(frame_len);
    let mut buf = unwrap_or_bail!(buf_result, zx::ZX_ERR_NO_RESOURCES);
    // It is safe here because `payload_slice` does not outlive `payload`.
    let payload_slice = unsafe { utils::as_slice(payload, payload_len) };
    let write_result =
        client::write_eth_frame(&mut buf[..], *dst_addr, *src_addr, protocol_id, payload_slice);
    let written_bytes = unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL).written_bytes();
    *out_buf = OutBuf::from(buf, written_bytes);
    zx::ZX_OK
}
