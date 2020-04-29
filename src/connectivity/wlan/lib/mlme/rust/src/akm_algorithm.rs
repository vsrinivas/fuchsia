// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::auth,
    anyhow::{bail, Error},
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    log::error,
    wlan_common::{mac, time::TimeUnit},
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
    /// The underlying type used to represent timers.
    type EventId;
    /// Transmit an auth frame to the peer in this auth exchange.
    fn send_auth_frame(
        &mut self,
        auth_type: mac::AuthAlgorithmNumber,
        seq_num: u16,
        result_code: mac::StatusCode,
        auth_content: &[u8],
    ) -> Result<(), Error>;
    /// Inform SME that this exchange has produced a PMK.
    fn publish_pmk(&mut self, pmk: fidl_mlme::PmkInfo);
    /// Schedule a new timeout that will fire after the given duration has elapsed. The returned
    /// ID should be unique from any other timeout that has been scheduled but not cancelled.
    fn schedule_auth_timeout(&mut self, duration: TimeUnit) -> Self::EventId;
    fn cancel_auth_timeout(&mut self, id: Self::EventId);
}

/// An algorithm used to perform authentication and optionally generate a PMK.
#[derive(Debug)]
pub enum AkmAlgorithm<T> {
    _OpenAp,
    OpenSupplicant { timeout_bcn_count: u16, timeout: Option<T> },
    _Sae,
}

impl<T> AkmAlgorithm<T> {
    pub fn auth_type(&self) -> fidl_mlme::AuthenticationTypes {
        match self {
            AkmAlgorithm::_OpenAp { .. } => fidl_mlme::AuthenticationTypes::OpenSystem,
            AkmAlgorithm::OpenSupplicant { .. } => fidl_mlme::AuthenticationTypes::OpenSystem,
            AkmAlgorithm::_Sae { .. } => fidl_mlme::AuthenticationTypes::Sae,
        }
    }
}

impl<T: Eq + Clone> AkmAlgorithm<T> {
    pub fn open_supplicant(timeout_bcn_count: u16) -> Self {
        AkmAlgorithm::OpenSupplicant { timeout_bcn_count, timeout: None }
    }

    pub fn initiate<A: AkmAction<EventId = T>>(
        &mut self,
        actions: &mut A,
    ) -> Result<AkmState, Error> {
        match self {
            AkmAlgorithm::_OpenAp => {
                error!("OpenAp AKM does not support initiating an auth exchange.");
                Ok(AkmState::Failed)
            }
            AkmAlgorithm::OpenSupplicant { timeout_bcn_count, timeout } => {
                actions.send_auth_frame(
                    mac::AuthAlgorithmNumber::OPEN,
                    1,
                    mac::StatusCode::SUCCESS,
                    &[],
                )?;
                let duration = TimeUnit::DEFAULT_BEACON_INTERVAL * timeout_bcn_count.clone();
                timeout.replace(actions.schedule_auth_timeout(duration));
                Ok(AkmState::InProgress)
            }
            AkmAlgorithm::_Sae => bail!("Sae akm not yet implemented"),
        }
    }

    pub fn handle_auth_frame<A: AkmAction<EventId = T>>(
        &mut self,
        actions: &mut A,
        hdr: &mac::AuthHdr,
        _body: Option<&[u8]>,
    ) -> Result<AkmState, Error> {
        match self {
            AkmAlgorithm::_OpenAp => bail!("OpenAp akm not yet implemented"),
            AkmAlgorithm::OpenSupplicant { timeout, .. } => {
                timeout.take().map(|timeout| actions.cancel_auth_timeout(timeout));
                match auth::validate_ap_resp(hdr) {
                    Ok(auth::ValidFrame::Open) => Ok(AkmState::AuthComplete),
                    Ok(frame_type) => {
                        error!("Received unhandled auth frame type {:?}", frame_type);
                        Ok(AkmState::Failed)
                    }
                    Err(e) => {
                        error!("Received invalid auth frame: {}", e);
                        Ok(AkmState::Failed)
                    }
                }
            }
            AkmAlgorithm::_Sae => bail!("Sae akm not yet implemented"),
        }
    }

    pub fn handle_timeout<A: AkmAction<EventId = T>>(
        &mut self,
        actions: &mut A,
        event: T,
    ) -> Result<AkmState, Error> {
        match self {
            AkmAlgorithm::_OpenAp => bail!("OpenAp akm not yet implemented"),
            AkmAlgorithm::OpenSupplicant { timeout, .. } => {
                if let Some(timeout) = timeout.take() {
                    if timeout == event {
                        actions.cancel_auth_timeout(timeout);
                        Ok(AkmState::Failed)
                    } else {
                        Ok(AkmState::InProgress)
                    }
                } else {
                    Ok(AkmState::AuthComplete)
                }
            }
            AkmAlgorithm::_Sae => bail!("Sae akm not yet implemented"),
        }
    }

    pub fn cancel<A: AkmAction<EventId = T>>(&mut self, actions: &mut A) {
        match self {
            AkmAlgorithm::_OpenAp => (),
            AkmAlgorithm::OpenSupplicant { timeout, .. } => {
                if let Some(timeout) = timeout.take() {
                    actions.cancel_auth_timeout(timeout);
                }
            }
            AkmAlgorithm::_Sae => (),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, wlan_common::assert_variant};

    struct MockAkmAction {
        sent_frames: Vec<(mac::AuthAlgorithmNumber, u16, mac::StatusCode, Vec<u8>)>,
        accept_frames: bool,
        published_pmks: Vec<fidl_mlme::PmkInfo>,
        scheduled_timers: Vec<u16>,
        timer_counter: u16,
    }

    impl MockAkmAction {
        fn new() -> Self {
            MockAkmAction {
                sent_frames: vec![],
                accept_frames: true,
                published_pmks: vec![],
                scheduled_timers: vec![],
                timer_counter: 0,
            }
        }
    }

    impl AkmAction for MockAkmAction {
        type EventId = u16;
        fn send_auth_frame(
            &mut self,
            auth_type: mac::AuthAlgorithmNumber,
            seq_num: u16,
            result_code: mac::StatusCode,
            auth_content: &[u8],
        ) -> Result<(), Error> {
            if self.accept_frames {
                self.sent_frames.push((auth_type, seq_num, result_code, auth_content.to_vec()));
                Ok(())
            } else {
                bail!("send_auth_frames disabled by test");
            }
        }

        fn publish_pmk(&mut self, pmk: fidl_mlme::PmkInfo) {
            self.published_pmks.push(pmk);
        }

        fn schedule_auth_timeout(&mut self, _duration: TimeUnit) -> Self::EventId {
            self.timer_counter += 1;
            self.scheduled_timers.push(self.timer_counter);
            self.timer_counter
        }

        fn cancel_auth_timeout(&mut self, id: Self::EventId) {
            self.scheduled_timers =
                self.scheduled_timers.iter().cloned().filter(|e| e != &id).collect();
        }
    }

    #[test]
    fn open_supplicant_success() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::open_supplicant(10);

        // Initiate sends
        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.sent_frames.len(), 1);
        assert_eq!(
            actions.sent_frames.remove(0),
            (mac::AuthAlgorithmNumber::OPEN, 1, mac::StatusCode::SUCCESS, vec![])
        );
        assert_eq!(actions.scheduled_timers.len(), 1);

        // A valid response completes auth.
        let hdr = mac::AuthHdr {
            auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
            auth_txn_seq_num: 2,
            status_code: mac::StatusCode::SUCCESS,
        };
        assert_variant!(
            supplicant.handle_auth_frame(&mut actions, &hdr, None),
            Ok(AkmState::AuthComplete)
        );

        // Everything is cleaned up.
        assert_eq!(actions.sent_frames.len(), 0);
        assert_eq!(actions.published_pmks.len(), 0);
        assert_eq!(actions.scheduled_timers.len(), 0); // Timer cancelled.
    }

    #[test]
    fn open_supplicant_cancel() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::open_supplicant(10);

        // Initiate sends
        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.sent_frames.len(), 1);
        assert_eq!(actions.scheduled_timers.len(), 1);
        actions.sent_frames.clear();

        supplicant.cancel(&mut actions);
        assert_eq!(actions.scheduled_timers.len(), 0);
        assert_eq!(actions.sent_frames.len(), 0);
        assert_eq!(actions.published_pmks.len(), 0);
    }

    #[test]
    fn open_supplicant_timeout() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::open_supplicant(10);

        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.scheduled_timers.len(), 1);
        let timeout = actions.scheduled_timers[0];

        assert_variant!(supplicant.handle_timeout(&mut actions, timeout), Ok(AkmState::Failed));
        assert_eq!(actions.scheduled_timers.len(), 0);
    }

    #[test]
    fn open_supplicant_ignored_timeout() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::open_supplicant(10);

        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.scheduled_timers.len(), 1);

        // We do nothing in response to an unrecognized timeout.
        assert_variant!(supplicant.handle_timeout(&mut actions, 100), Ok(AkmState::InProgress));
        assert_eq!(actions.scheduled_timers.len(), 1);
    }

    #[test]
    fn open_supplicant_reject() {
        let mut actions = MockAkmAction::new();
        let mut supplicant = AkmAlgorithm::open_supplicant(10);

        // Initiate sends
        assert_variant!(supplicant.initiate(&mut actions), Ok(AkmState::InProgress));
        assert_eq!(actions.sent_frames.len(), 1);
        actions.sent_frames.clear();
        assert_eq!(actions.scheduled_timers.len(), 1);

        // A rejected response ends auth.
        let hdr = mac::AuthHdr {
            auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
            auth_txn_seq_num: 2,
            status_code: mac::StatusCode::REFUSED,
        };
        assert_variant!(
            supplicant.handle_auth_frame(&mut actions, &hdr, None),
            Ok(AkmState::Failed)
        );

        // Everything is cleaned up.
        assert_eq!(actions.sent_frames.len(), 0);
        assert_eq!(actions.published_pmks.len(), 0);
        assert_eq!(actions.scheduled_timers.len(), 0); // Timer cancelled.
    }
}
