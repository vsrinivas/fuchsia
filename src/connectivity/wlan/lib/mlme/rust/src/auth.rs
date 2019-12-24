// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{ensure, Error},
    wlan_common::mac,
};

pub fn make_open_client_req() -> mac::AuthHdr {
    mac::AuthHdr {
        auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
        auth_txn_seq_num: 1,
        status_code: mac::StatusCode::SUCCESS,
    }
}

/// Validates whether a given authentication header is a valid response to an open authentication
/// request.
pub fn is_valid_open_ap_resp(auth: &mac::AuthHdr) -> Result<(), Error> {
    ensure!(
        { auth.auth_alg_num } == mac::AuthAlgorithmNumber::OPEN,
        "invalid auth_alg_num: {}",
        { auth.auth_alg_num }.0
    );
    ensure!(auth.auth_txn_seq_num == 2, "invalid auth_txn_seq_num: {}", { auth.auth_txn_seq_num });
    ensure!(
        { auth.status_code } == mac::StatusCode::SUCCESS,
        "invalid status_code: {}",
        { auth.status_code }.0
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_valid_auth_resp() -> mac::AuthHdr {
        mac::AuthHdr {
            auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
            auth_txn_seq_num: 2,
            status_code: mac::StatusCode::SUCCESS,
        }
    }

    #[test]
    fn valid_open_auth_resp() {
        assert!(is_valid_open_ap_resp(&make_valid_auth_resp()).is_ok());
    }

    #[test]
    fn invalid_open_auth_resp() {
        let mut auth_hdr = make_valid_auth_resp();
        auth_hdr.auth_alg_num = mac::AuthAlgorithmNumber::FAST_BSS_TRANSITION;
        assert!(is_valid_open_ap_resp(&auth_hdr).is_err());

        let mut auth_hdr = make_valid_auth_resp();
        auth_hdr.auth_txn_seq_num = 1;
        assert!(is_valid_open_ap_resp(&auth_hdr).is_err());

        let mut auth_hdr = make_valid_auth_resp();
        auth_hdr.status_code = mac::StatusCode::REFUSED;
        assert!(is_valid_open_ap_resp(&auth_hdr).is_err());
    }
}
