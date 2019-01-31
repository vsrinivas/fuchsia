// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::buffer::InBuf,
    fuchsia_zircon as zx,
    log::error,
    wlan_mlme::{auth, common::mac},
};

#[no_mangle]
pub extern "C" fn rust_mlme_is_valid_open_auth_resp(buf: InBuf, has_body_aligned: bool) -> i32 {
    match mac::MacFrame::parse(&buf[..], has_body_aligned) {
        Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
            let fc = mac::FrameControl(mgmt_hdr.frame_ctrl());
            match mac::MgmtSubtype::parse(fc.frame_subtype(), body) {
                Some(mac::MgmtSubtype::Authentication { auth_hdr, .. }) => {
                    let status: zx::Status = auth::is_valid_open_auth_resp(&auth_hdr)
                        .map_err(|e| {
                            error!("error, invalid Authentication response: {}", e);
                            zx::Status::IO_REFUSED
                        })
                        .into();
                    status.into_raw()
                }
                _ => zx::sys::ZX_ERR_IO_REFUSED,
            }
        }
        _ => zx::sys::ZX_ERR_IO_REFUSED,
    }
}
