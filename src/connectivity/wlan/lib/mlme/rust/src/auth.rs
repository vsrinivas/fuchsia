// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, ensure, Error},
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    wlan_common::mac,
};

pub fn make_open_client_req() -> mac::AuthHdr {
    mac::AuthHdr {
        auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
        auth_txn_seq_num: 1,
        status_code: fidl_ieee80211::StatusCode::Success.into(),
    }
}

#[derive(Debug)]
pub enum ValidFrame {
    Open,
    SaeCommit,
    SaeConfirm,
}

/// Validates whether a given authentication header is a valid response to an authentication
/// request.
pub fn validate_ap_resp(auth: &mac::AuthHdr) -> Result<ValidFrame, Error> {
    ensure!(
        { auth.status_code } == fidl_ieee80211::StatusCode::Success.into(),
        "invalid status_code: {}",
        { auth.status_code }.0
    );
    match auth.auth_alg_num {
        mac::AuthAlgorithmNumber::OPEN => {
            ensure!(auth.auth_txn_seq_num == 2, "invalid auth_txn_seq_num: {}", {
                auth.auth_txn_seq_num
            });
            Ok(ValidFrame::Open)
        }
        mac::AuthAlgorithmNumber::SAE => match auth.auth_txn_seq_num {
            1 => Ok(ValidFrame::SaeCommit),
            2 => Ok(ValidFrame::SaeConfirm),
            _ => bail!("invalid auth_txn_seq_num: {}", { auth.auth_txn_seq_num }),
        },
        _ => bail!("invalid auth_alg_num: {}", { auth.auth_alg_num }.0),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, wlan_common::assert_variant};

    fn make_valid_auth_resp(frame_type: ValidFrame) -> mac::AuthHdr {
        mac::AuthHdr {
            auth_alg_num: match frame_type {
                ValidFrame::Open => mac::AuthAlgorithmNumber::OPEN,
                ValidFrame::SaeCommit | ValidFrame::SaeConfirm => mac::AuthAlgorithmNumber::SAE,
            },
            auth_txn_seq_num: match frame_type {
                ValidFrame::SaeCommit => 1,
                ValidFrame::Open | ValidFrame::SaeConfirm => 2,
            },
            status_code: fidl_ieee80211::StatusCode::Success.into(),
        }
    }

    #[test]
    fn valid_auth_resp() {
        assert_variant!(
            validate_ap_resp(&make_valid_auth_resp(ValidFrame::Open)),
            Ok(ValidFrame::Open)
        );
        assert_variant!(
            validate_ap_resp(&make_valid_auth_resp(ValidFrame::SaeCommit)),
            Ok(ValidFrame::SaeCommit)
        );
        assert_variant!(
            validate_ap_resp(&make_valid_auth_resp(ValidFrame::SaeConfirm)),
            Ok(ValidFrame::SaeConfirm)
        );
    }

    #[test]
    fn invalid_auth_resp() {
        let mut auth_hdr = make_valid_auth_resp(ValidFrame::Open);
        auth_hdr.auth_alg_num = mac::AuthAlgorithmNumber::FAST_BSS_TRANSITION;
        assert_variant!(validate_ap_resp(&auth_hdr), Err(_));

        let mut auth_hdr = make_valid_auth_resp(ValidFrame::Open);
        auth_hdr.auth_txn_seq_num = 1;
        assert_variant!(validate_ap_resp(&auth_hdr), Err(_));

        let mut auth_hdr = make_valid_auth_resp(ValidFrame::Open);
        auth_hdr.status_code = fidl_ieee80211::StatusCode::RefusedReasonUnspecified.into();
        assert_variant!(validate_ap_resp(&auth_hdr), Err(_));

        let mut auth_hdr = make_valid_auth_resp(ValidFrame::SaeCommit);
        auth_hdr.auth_txn_seq_num = 4;
        assert_variant!(validate_ap_resp(&auth_hdr), Err(_));

        let mut auth_hdr = make_valid_auth_resp(ValidFrame::SaeCommit);
        auth_hdr.status_code = fidl_ieee80211::StatusCode::RefusedReasonUnspecified.into();
        assert_variant!(validate_ap_resp(&auth_hdr), Err(_));
    }
}
