// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{ensure, Error},
    wlan_common::mac,
};

/// Validates whether a given authentication header is a valid response to an open authentication
/// request.
pub fn is_valid_open_auth_resp(auth: &mac::AuthHdr) -> Result<(), Error> {
    ensure!(
        auth.auth_alg_num() == mac::AuthAlgorithm::Open as u16,
        "invalid auth_alg_num: {}",
        auth.auth_alg_num()
    );
    ensure!(auth.auth_txn_seq_num() == 2, "invalid auth_txn_seq_num: {}", auth.auth_txn_seq_num());
    ensure!(
        auth.status_code() == mac::StatusCode::Success as u16,
        "invalid status_code: {}",
        auth.status_code()
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_valid_auth_resp() -> mac::AuthHdr {
        let mut auth_hdr = mac::AuthHdr::default();
        auth_hdr.set_auth_alg_num(0);
        auth_hdr.set_auth_txn_seq_num(2);
        auth_hdr.set_status_code(0);
        auth_hdr
    }

    #[test]
    fn valid_open_auth_resp() {
        assert!(is_valid_open_auth_resp(&make_valid_auth_resp()).is_ok());
    }

    #[test]
    fn invalid_open_auth_resp() {
        let mut auth_hdr = make_valid_auth_resp();
        auth_hdr.set_auth_alg_num(2);
        assert!(is_valid_open_auth_resp(&auth_hdr).is_err());

        let mut auth_hdr = make_valid_auth_resp();
        auth_hdr.set_auth_txn_seq_num(1);
        assert!(is_valid_open_auth_resp(&auth_hdr).is_err());

        let mut auth_hdr = make_valid_auth_resp();
        auth_hdr.set_status_code(1);
        assert!(is_valid_open_auth_resp(&auth_hdr).is_err());
    }
}
