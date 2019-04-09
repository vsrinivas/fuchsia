// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils, fuchsia_zircon::sys as zx, log::error, wlan_common::mac, wlan_mlme::auth,
    zerocopy::LayoutVerified,
};

#[no_mangle]
pub extern "C" fn mlme_is_valid_open_auth_resp(data: *const u8, len: usize) -> i32 {
    // `slice` does not outlive `data`.
    let slice = unsafe { utils::as_slice(data, len) };
    match LayoutVerified::<_, mac::AuthHdr>::new_unaligned_from_prefix(slice) {
        Some((auth_hdr, _)) => {
            unwrap_or_bail!(auth::is_valid_open_ap_resp(&auth_hdr), zx::ZX_ERR_IO_REFUSED);
            zx::ZX_OK
        }
        None => zx::ZX_ERR_IO_REFUSED,
    }
}
