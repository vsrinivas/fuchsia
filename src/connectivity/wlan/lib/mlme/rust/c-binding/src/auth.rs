// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::sys as zx, log::error, wlan_common::mac, wlan_mlme::auth,
    zerocopy::LayoutVerified,
};

#[no_mangle]
pub extern "C" fn mlme_is_valid_open_auth_resp(auth_resp: wlan_span::CSpan<'_>) -> i32 {
    // `slice` does not outlive `auth_resp`.
    let slice: &[u8] = auth_resp.into();
    match LayoutVerified::<_, mac::AuthHdr>::new_unaligned_from_prefix(slice) {
        Some((auth_hdr, _)) => {
            unwrap_or_bail!(auth::is_valid_open_ap_resp(&auth_hdr), zx::ZX_ERR_IO_REFUSED);
            zx::ZX_OK
        }
        None => zx::ZX_ERR_IO_REFUSED,
    }
}
