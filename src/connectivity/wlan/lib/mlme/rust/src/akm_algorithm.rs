// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::auth,
    anyhow::{bail, Error},
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    log::error,
    wlan_common::mac,
};

/// AkmState indicates the current status of authentication after each event is handled by an
/// AkmAlgorithm.
#[derive(Debug)]
pub enum AkmState {
    /// Authentication is proceeding as expected.
    InProgress,
    /// Authentication has been cancelled, rejected, or ended.
    Failed,
    /// Authentication is complete and we should proceed to association.
    AuthComplete,
}

/// AkmAction allows an AkmAlgorithm to interact with the rest of MLME without tying the
/// implementation to a particular type of STA.
pub trait AkmAction {
    /// Transmit an auth frame to the peer in this auth exchange.
    fn send_auth_frame(
        &mut self,
        auth_type: mac::AuthAlgorithmNumber,
        seq_num: u16,
        status_code: mac::StatusCode,
        auth_content: &[u8],
    ) -> Result<(), Error>;
    /// Transmit information for an SME-managed SAE handshaek
    fn forward_sme_sae_rx(
        &mut self,
        seq_num: u16,
        status_code: fidl_ieee80211::StatusCode,
        sae_fields: Vec<u8>,
    );
    fn forward_sae_handshake_ind(&mut self);
}

/// An algorithm used to perform authentication and optionally generate a PMK.
#[allow(unused)]
pub enum AkmAlgorithm {
    _OpenAp,
    OpenSupplicant,
    Sae,
}

impl std::fmt::Debug for AkmAlgorithm {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        f.write_str(match self {
            AkmAlgorithm::_OpenAp { .. } => "Open authentication AP",
            AkmAlgorithm::OpenSupplicant { .. } => "Open authentication SAE",
            AkmAlgorithm::Sae { .. } => "SAE authentication",
        })
    }
}

impl AkmAlgorithm {
    pub fn initiate<A: AkmAction>(&mut self, actions: &mut A) -> Result<AkmState, Error> {
        match self {
            AkmAlgorithm::_OpenAp => {
                error!("OpenAp AKM does not support initiating an auth exchange.");
                Ok(AkmState::Failed)
            }
            AkmAlgorithm::OpenSupplicant => {
                actions.send_auth_frame(
                    mac::AuthAlgorithmNumber::OPEN,
                    1,
                    fidl_ieee80211::StatusCode::Success.into(),
                    &[],
                )?;
                Ok(AkmState::InProgress)
            }
            AkmAlgorithm::Sae => {
                actions.forward_sae_handshake_ind();
                Ok(AkmState::InProgress)
            }
        }
    }

    pub fn handle_auth_frame<A: AkmAction>(
        &mut self,
        actions: &mut A,
        hdr: &mac::AuthHdr,
        body: Option<&[u8]>,
    ) -> Result<AkmState, Error> {
        match self {
            AkmAlgorithm::_OpenAp => bail!("OpenAp akm not yet implemented"),
            AkmAlgorithm::OpenSupplicant { .. } => match auth::validate_ap_resp(hdr) {
                Ok(auth::ValidFrame::Open) => Ok(AkmState::AuthComplete),
                Ok(frame_type) => {
                    error!("Received unhandled auth frame type {:?}", frame_type);
                    Ok(AkmState::Failed)
                }
                Err(e) => {
                    error!("Received invalid auth frame: {}", e);
                    Ok(AkmState::Failed)
                }
            },
            AkmAlgorithm::Sae { .. } => {
                let sae_fields = body.map(|body| body.to_vec()).unwrap_or(vec![]);
                actions.forward_sme_sae_rx(
                    hdr.auth_txn_seq_num,
                    // TODO(fxbug.dev/91353): All reserved values mapped to REFUSED_REASON_UNSPECIFIED.
                    Option::<fidl_ieee80211::StatusCode>::from(hdr.status_code)
                        .unwrap_or(fidl_ieee80211::StatusCode::RefusedReasonUnspecified),
                    sae_fields,
                );
                Ok(AkmState::InProgress)
            }
        }
    }

    pub fn handle_sae_resp<A: AkmAction>(
        &mut self,
        _actions: &mut A,
        status_code: fidl_ieee80211::StatusCode,
    ) -> Result<AkmState, Error> {
        match self {
            AkmAlgorithm::_OpenAp => bail!("OpenAp akm not yet implemented"),
            AkmAlgorithm::OpenSupplicant { .. } => {
                bail!("Open supplicant doesn't expect an SaeResp")
            }
            AkmAlgorithm::Sae { .. } => match status_code {
                fidl_ieee80211::StatusCode::Success => Ok(AkmState::AuthComplete),
                _ => Ok(AkmState::Failed),
            },
        }
    }

    pub fn handle_sme_sae_tx<A: AkmAction>(
        &mut self,
        actions: &mut A,
        seq_num: u16,
        status_code: fidl_ieee80211::StatusCode,
        sae_fields: &[u8],
    ) -> Result<AkmState, Error> {
        match self {
            AkmAlgorithm::_OpenAp => bail!("OpenAp akm not yet implemented"),
            AkmAlgorithm::OpenSupplicant { .. } => {
                bail!("Open supplicant cannot transmit SAE frames")
            }
            AkmAlgorithm::Sae { .. } => {
                actions.send_auth_frame(
                    mac::AuthAlgorithmNumber::SAE,
                    seq_num,
                    status_code.into(),
                    sae_fields,
                )?;
                // The handshake may be complete at this point, but we wait for an SaeResp.
                Ok(AkmState::InProgress)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_wlan_mlme as fidl_mlme, wlan_common::assert_variant};

    struct MockAkmAction {
        sent_frames: Vec<(mac::AuthAlgorithmNumber, u16, mac::StatusCode, Vec<u8>)>,
        sent_sae_rx: Vec<(u16, fidl_ieee80211::StatusCode, Vec<u8>)>,
        accept_frames: bool,
        published_pmks: Vec<fidl_mlme::PmkInfo>,
        sae_ind_sent: u16,
    }

    impl MockAkmAction {
        fn new() -> Self {
            MockAkmAction {
                sent_frames: vec![],
                sent_sae_rx: vec![],
                accept_frames: true,
                published_pmks: vec![],
                sae_ind_sent: 0,
            }
        }
    }

    impl AkmAction for MockAkmAction {
        fn send_auth_frame(
            &mut self,
            auth_type: mac::AuthAlgorithmNumber,
            seq_num: u16,
            status_code: mac::StatusCode,
            auth_content: &[u8],
        ) -> Result<(), Error> {
            if self.accept_frames {
                self.sent_frames.push((auth_type, seq_num, status_code, auth_content.to_vec()));
                Ok(())
            } else {
                bail!("send_auth_frames disabled by test");
            }
        }

        fn forward_sme_sae_rx(
            &mut self,
            seq_num: u16,
            status_code: fidl_ieee80211::StatusCode,
            sae_fields: Vec<u8>,
        ) {
            self.sent_sae_rx.push((seq_num, status_code, sae_fields))
        }

        fn forward_sae_handshake_ind(&mut self) {
            self.sae_ind_sent += 1
        }
    }

    #[test]
    fn open_supplicant_success() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::OpenSupplicant;

        // Initiate sends
        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.sent_frames.len(), 1);
        assert_eq!(
            actions.sent_frames.remove(0),
            (mac::AuthAlgorithmNumber::OPEN, 1, fidl_ieee80211::StatusCode::Success.into(), vec![])
        );

        // A valid response completes auth.
        let hdr = mac::AuthHdr {
            auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
            auth_txn_seq_num: 2,
            status_code: fidl_ieee80211::StatusCode::Success.into(),
        };
        assert_variant!(
            supplicant.handle_auth_frame(&mut actions, &hdr, None),
            Ok(AkmState::AuthComplete)
        );

        // Everything is cleaned up.
        assert_eq!(actions.sent_frames.len(), 0);
        assert_eq!(actions.published_pmks.len(), 0);
    }

    #[test]
    fn open_supplicant_reject() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::OpenSupplicant;

        // Initiate sends
        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.sent_frames.len(), 1);
        actions.sent_frames.clear();

        // A rejected response ends auth.
        let hdr = mac::AuthHdr {
            auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
            auth_txn_seq_num: 2,
            status_code: fidl_ieee80211::StatusCode::RefusedReasonUnspecified.into(),
        };
        assert_variant!(
            supplicant.handle_auth_frame(&mut actions, &hdr, None),
            Ok(AkmState::Failed)
        );

        // Everything is cleaned up.
        assert_eq!(actions.sent_frames.len(), 0);
        assert_eq!(actions.published_pmks.len(), 0);
    }

    #[test]
    fn sae_supplicant_success() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::Sae;

        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.sae_ind_sent, 1);
        assert_eq!(actions.sent_frames.len(), 0);

        // We only test sending one frame each way, since there's no functional difference in the
        // second exchange.

        assert_variant!(
            supplicant.handle_sme_sae_tx(
                &mut actions,
                1,
                fidl_ieee80211::StatusCode::Success,
                &[0x12, 0x34][..],
            ),
            Ok(AkmState::InProgress)
        );
        assert_eq!(actions.sent_frames.len(), 1);
        assert_eq!(
            actions.sent_frames[0],
            (
                mac::AuthAlgorithmNumber::SAE,
                1,
                fidl_ieee80211::StatusCode::Success.into(),
                vec![0x12, 0x34]
            )
        );
        actions.sent_frames.clear();

        let hdr = mac::AuthHdr {
            auth_alg_num: mac::AuthAlgorithmNumber::SAE,
            auth_txn_seq_num: 1,
            status_code: fidl_ieee80211::StatusCode::Success.into(),
        };
        assert_variant!(
            supplicant.handle_auth_frame(&mut actions, &hdr, Some(&[0x56, 0x78][..])),
            Ok(AkmState::InProgress)
        );
        assert_eq!(actions.sent_sae_rx.len(), 1);
        assert_eq!(
            actions.sent_sae_rx[0],
            (1, fidl_ieee80211::StatusCode::Success, vec![0x56, 0x78])
        );
        actions.sent_sae_rx.clear();

        assert_variant!(
            supplicant.handle_sae_resp(&mut actions, fidl_ieee80211::StatusCode::Success),
            Ok(AkmState::AuthComplete)
        );
    }

    #[test]
    fn sae_supplicant_rejected() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::Sae;

        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_variant!(
            supplicant.handle_sae_resp(
                &mut actions,
                fidl_ieee80211::StatusCode::RefusedReasonUnspecified
            ),
            Ok(AkmState::Failed)
        );
    }
}
