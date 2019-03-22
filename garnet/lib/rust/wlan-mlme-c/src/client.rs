// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils,
    fuchsia_zircon::sys as zx,
    num_traits::FromPrimitive,
    wlan_common::buffer_writer::BufferWriter,
    wlan_mlme::{
        buffer::{BufferProvider, InBuf, OutBuf},
        client,
        common::{frame_len, mac, sequence::SequenceManager},
        device,
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
    let mut buf = unwrap_or_bail!(provider.get_buffer(frame_len), zx::ZX_ERR_NO_RESOURCES);
    let mut writer = BufferWriter::new(&mut buf[..]);
    let write_result = client::write_open_auth_frame(&mut writer, *bssid, *client_addr, seq_mgr);
    unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL);
    let bytes_written = writer.bytes_written();
    *out_buf = OutBuf::from(buf, bytes_written);
    zx::ZX_OK
}

#[no_mangle]
pub extern "C" fn mlme_write_deauth_frame(
    provider: BufferProvider,
    seq_mgr: &mut SequenceManager,
    bssid: &[u8; 6],
    client_addr: &[u8; 6],
    reason_code: u16,
    out_buf: &mut OutBuf,
) -> i32 {
    let frame_len = frame_len!(mac::MgmtHdr, mac::DeauthHdr);
    let buf_result = provider.get_buffer(frame_len);
    let mut buf = unwrap_or_bail!(buf_result, zx::ZX_ERR_NO_RESOURCES);
    let reason_code = mac::ReasonCode::from_u16(reason_code)
        .ok_or_else(|| format!("invalid reason code {}", reason_code));
    let reason_code = unwrap_or_bail!(reason_code, zx::ZX_ERR_INVALID_ARGS);
    let mut writer = BufferWriter::new(&mut buf[..]);
    let write_result =
        client::write_deauth_frame(&mut writer, *bssid, *client_addr, reason_code, seq_mgr);
    unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL);
    let bytes_written = writer.bytes_written();
    *out_buf = OutBuf::from(buf, bytes_written);
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
    let frame_len = frame_len!(mac::FixedDataHdrFields);
    let mut buf = unwrap_or_bail!(provider.get_buffer(frame_len), zx::ZX_ERR_NO_RESOURCES);
    let mut writer = BufferWriter::new(&mut buf[..]);
    let write_result =
        client::write_keep_alive_resp_frame(&mut writer, *bssid, *client_addr, seq_mgr);
    unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL);
    let bytes_written = writer.bytes_written();
    *out_buf = OutBuf::from(buf, bytes_written);
    zx::ZX_OK
}

#[no_mangle]
pub extern "C" fn mlme_handle_data_frame(
    device: &device::Device,
    data_frame: *const u8,
    data_frame_len: usize,
    has_padding: bool,
) -> i32 {
    // Safe here because |data_frame_slice| does not outlive |data_frame|.
    let data_frame_slice = unsafe { utils::as_slice(data_frame, data_frame_len) };
    client::handle_data_frame(device, data_frame_slice, has_padding);
    zx::ZX_OK
}

#[no_mangle]
pub unsafe extern "C" fn mlme_write_eapol_data_frame(
    provider: BufferProvider,
    seq_mgr: &mut SequenceManager,
    dest: &[u8; 6],
    src: &[u8; 6],
    is_protected: bool,
    eapol_frame_ptr: *const u8,
    eapol_frame_len: usize,
    out_buf: &mut OutBuf,
) -> i32 {
    let frame_len = frame_len!(mac::FixedDataHdrFields, mac::LlcHdr) + eapol_frame_len;
    let buf_result = provider.get_buffer(frame_len);
    let mut buf = unwrap_or_bail!(buf_result, zx::ZX_ERR_NO_RESOURCES);
    let mut writer = BufferWriter::new(&mut buf[..]);
    let eapol_frame = utils::as_slice(eapol_frame_ptr, eapol_frame_len);
    let write_result = client::write_eapol_data_frame(
        &mut writer,
        *dest,
        *src,
        seq_mgr,
        is_protected,
        eapol_frame,
    );
    unwrap_or_bail!(write_result, zx::ZX_ERR_INTERNAL);
    let written_bytes = writer.bytes_written();
    *out_buf = OutBuf::from(buf, written_bytes);
    zx::ZX_OK
}
