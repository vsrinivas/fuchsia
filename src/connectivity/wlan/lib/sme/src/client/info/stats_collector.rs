// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    crate::client::{ConnectFailure, ConnectResult},
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    ieee80211::Ssid,
    std::collections::VecDeque,
    thiserror::Error,
    wlan_rsn::{
        key::exchange::Key,
        rsna::{SecAssocStatus, SecAssocUpdate, UpdateSink},
    },
};

#[derive(Default)]
pub(crate) struct StatsCollector {
    discovery_scan_stats: Option<PendingScanStats>,
    /// Track successive connect attempts to the same SSID. This resets when attempt succeeds
    /// or when attempting to connect to a different SSID from a previous attempt.
    connect_attempts: Option<ConnectAttempts>,
    /// Track the most recent disconnection. Intended to be sent out on connection success
    /// so that client can compute time gap between last disconnect until reconnect.
    /// This is cleared out as soon as a connection attempt succeeds.
    previous_disconnect_info: Option<PreviousDisconnectInfo>,
}

impl StatsCollector {
    /// Report the start of a new discovery scan so that StatsCollector will begin collecting
    /// stats for it. If there's an existing pending scan stats, evict and return it.
    pub fn report_discovery_scan_started(
        &mut self,
        req: fidl_mlme::ScanRequest,
        is_connected: bool,
    ) -> Option<PendingScanStats> {
        let now = now();
        let pending_scan_stats =
            PendingScanStats { scan_start_at: now, req, scan_start_while_connected: is_connected };
        self.discovery_scan_stats.replace(pending_scan_stats)
    }

    pub fn report_discovery_scan_ended(
        &mut self,
        result_code: fidl_mlme::ScanResultCode,
        bss_count: usize,
    ) -> Result<ScanStats, StatsError> {
        let now = now();
        let pending_stats = self.discovery_scan_stats.take().ok_or(StatsError::NoPendingScan)?;

        let scan_stats = ScanStats {
            scan_type: pending_stats.req.scan_type,
            scan_start_while_connected: pending_stats.scan_start_while_connected,
            scan_start_at: pending_stats.scan_start_at,
            scan_end_at: now,
            result_code,
            bss_count,
        };
        Ok(scan_stats)
    }

    /// Report the start of a new connect attempt so that StatsCollector will begin collecting
    /// stats for it. If there's an existing pending connect stats, evict and return it.
    pub fn report_connect_started(&mut self, ssid: Ssid) -> Option<PendingConnectStats> {
        match self.connect_attempts.as_mut() {
            Some(connect_attempts) => connect_attempts.update_with_new_attempt(ssid),
            None => {
                self.connect_attempts = Some(ConnectAttempts::new(ssid));
                None
            }
        }
    }

    pub fn report_candidate_network(
        &mut self,
        candidate_network: CandidateNetwork,
    ) -> Result<(), StatsError> {
        self.connect_stats()?.candidate_network.replace(candidate_network);
        Ok(())
    }

    pub fn report_auth_started(&mut self) -> Result<(), StatsError> {
        self.connect_stats()?.auth_start_at.replace(now());
        Ok(())
    }

    pub fn report_assoc_started(&mut self) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats()?;
        let now = now();
        pending_stats.auth_end_at.replace(now);
        pending_stats.assoc_start_at.replace(now);
        Ok(())
    }

    pub fn report_assoc_success(&mut self) -> Result<(), StatsError> {
        self.connect_stats()?.assoc_end_at.replace(now());
        Ok(())
    }

    pub fn report_rsna_started(&mut self) -> Result<(), StatsError> {
        self.connect_stats()?.rsna_start_at.replace(now());
        Ok(())
    }

    pub fn report_rsna_established(&mut self) -> Result<(), StatsError> {
        self.connect_stats()?.rsna_end_at.replace(now());
        Ok(())
    }

    /// Report updates derived from the supplicant. Used to record progress of establish RSNA step.
    pub fn report_supplicant_updates(
        &mut self,
        update_sink: &UpdateSink,
    ) -> Result<(), StatsError> {
        let supplicant_progress =
            self.connect_stats()?.supplicant_progress.get_or_insert(SupplicantProgress::default());
        for update in update_sink {
            match update {
                SecAssocUpdate::Status(status) => match status {
                    SecAssocStatus::PmkSaEstablished => {
                        supplicant_progress.pmksa_established = true
                    }
                    SecAssocStatus::EssSaEstablished => {
                        supplicant_progress.esssa_established = true
                    }
                    _ => (),
                },
                SecAssocUpdate::Key(key) => match key {
                    Key::Ptk(..) => supplicant_progress.ptksa_established = true,
                    Key::Gtk(..) => supplicant_progress.gtksa_established = true,
                    _ => (),
                },
                _ => (),
            }
        }
        Ok(())
    }

    pub fn report_supplicant_error(&mut self, error: anyhow::Error) -> Result<(), StatsError> {
        self.connect_stats()?.supplicant_error.replace(error);
        Ok(())
    }

    pub fn report_key_exchange_timeout(&mut self) -> Result<(), StatsError> {
        self.connect_stats()?.num_rsna_key_frame_exchange_timeout += 1;
        Ok(())
    }

    pub fn report_connect_finished(
        &mut self,
        result: ConnectResult,
    ) -> Result<ConnectStats, StatsError> {
        let mut connect_attempts =
            self.connect_attempts.take().ok_or(StatsError::NoPendingConnect)?;
        let stats = self.finalize_connect_stats(&mut connect_attempts, result.clone())?;

        if result != ConnectResult::Success {
            // Successive connect attempts are still tracked unless connection is successful
            self.connect_attempts.replace(connect_attempts);
        }
        Ok(stats)
    }

    fn connect_stats(&mut self) -> Result<&mut PendingConnectStats, StatsError> {
        self.connect_attempts
            .as_mut()
            .and_then(|a| a.stats.as_mut())
            .ok_or(StatsError::NoPendingConnect)
    }

    fn finalize_connect_stats(
        &mut self,
        connect_attempts: &mut ConnectAttempts,
        result: ConnectResult,
    ) -> Result<ConnectStats, StatsError> {
        let now = now();
        let pending_stats = connect_attempts.handle_result(&result, now)?;
        let previous_disconnect_info = match &result {
            ConnectResult::Success => self.previous_disconnect_info.take(),
            _ => None,
        };

        Ok(ConnectStats {
            connect_start_at: pending_stats.connect_start_at,
            connect_end_at: now,
            auth_start_at: pending_stats.auth_start_at,
            auth_end_at: pending_stats.auth_end_at,
            assoc_start_at: pending_stats.assoc_start_at,
            assoc_end_at: pending_stats.assoc_end_at,
            rsna_start_at: pending_stats.rsna_start_at,
            rsna_end_at: pending_stats.rsna_end_at,
            result,
            candidate_network: pending_stats.candidate_network,
            supplicant_error: pending_stats.supplicant_error,
            supplicant_progress: pending_stats.supplicant_progress,
            num_rsna_key_frame_exchange_timeout: pending_stats.num_rsna_key_frame_exchange_timeout,
            attempts: connect_attempts.attempts,
            last_ten_failures: connect_attempts.last_ten_failures.iter().cloned().collect(),
            previous_disconnect_info,
        })
    }

    pub fn report_disconnect(&mut self, ssid: Ssid, source: DisconnectSource) {
        self.previous_disconnect_info.replace(PreviousDisconnectInfo {
            ssid,
            disconnect_source: source,
            disconnect_at: now(),
        });
    }
}

fn now() -> zx::Time {
    zx::Time::get_monotonic()
}

struct ConnectAttempts {
    ssid: Ssid,
    attempts: u32,
    last_ten_failures: VecDeque<ConnectFailure>,
    stats: Option<PendingConnectStats>,
}

impl ConnectAttempts {
    const MAX_FAILURES_TRACKED: usize = 10;

    pub fn new(ssid: Ssid) -> Self {
        Self {
            ssid,
            attempts: 1,
            last_ten_failures: VecDeque::with_capacity(Self::MAX_FAILURES_TRACKED),
            stats: Some(PendingConnectStats::new(now())),
        }
    }

    /// Increment attempt counter if same SSID. Otherwise, reset and start tracking connect
    /// attempts for the new SSID.
    ///
    /// Additionally, evict and return any existing PendingConnectStats.
    pub fn update_with_new_attempt(&mut self, ssid: Ssid) -> Option<PendingConnectStats> {
        if self.ssid == ssid {
            self.attempts += 1;
        } else {
            self.ssid = ssid;
            self.attempts = 1;
            self.last_ten_failures.clear();
        }
        self.stats.replace(PendingConnectStats::new(now()))
    }

    pub fn handle_result(
        &mut self,
        result: &ConnectResult,
        now: zx::Time,
    ) -> Result<PendingConnectStats, StatsError> {
        let mut pending_stats = self.stats.take().ok_or(StatsError::NoPendingConnect)?;
        match result {
            ConnectResult::Failed(failure) => match failure {
                ConnectFailure::AuthenticationFailure(..) => {
                    pending_stats.auth_end_at.replace(now);
                }
                ConnectFailure::AssociationFailure(..) => {
                    pending_stats.assoc_end_at.replace(now);
                }
                ConnectFailure::EstablishRsnaFailure(..) => {
                    pending_stats.rsna_end_at.replace(now);
                }
                _ => (),
            },
            _ => (),
        }

        if let ConnectResult::Failed(failure) = result {
            if self.last_ten_failures.len() >= Self::MAX_FAILURES_TRACKED {
                self.last_ten_failures.pop_front();
            }
            self.last_ten_failures.push_back(failure.clone());
        }

        Ok(pending_stats)
    }
}

#[derive(Debug, Error)]
pub(crate) enum StatsError {
    #[error("no current pending connect")]
    NoPendingConnect,
    #[error("no current pending scan")]
    NoPendingScan,
}

pub(crate) struct PendingScanStats {
    scan_start_at: zx::Time,
    req: fidl_mlme::ScanRequest,
    scan_start_while_connected: bool,
}

pub(crate) struct PendingConnectStats {
    connect_start_at: zx::Time,
    auth_start_at: Option<zx::Time>,
    auth_end_at: Option<zx::Time>,
    assoc_start_at: Option<zx::Time>,
    assoc_end_at: Option<zx::Time>,
    rsna_start_at: Option<zx::Time>,
    rsna_end_at: Option<zx::Time>,
    candidate_network: Option<CandidateNetwork>,
    supplicant_error: Option<anyhow::Error>,
    supplicant_progress: Option<SupplicantProgress>,
    num_rsna_key_frame_exchange_timeout: u32,
}

impl PendingConnectStats {
    fn new(connect_start_at: zx::Time) -> Self {
        Self {
            connect_start_at,
            auth_start_at: None,
            auth_end_at: None,
            assoc_start_at: None,
            assoc_end_at: None,
            rsna_start_at: None,
            rsna_end_at: None,
            candidate_network: None,
            supplicant_error: None,
            supplicant_progress: None,
            num_rsna_key_frame_exchange_timeout: 0,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::{
            test_utils::fake_scan_request, EstablishRsnaFailure, EstablishRsnaFailureReason,
            SelectNetworkFailure,
        },
        anyhow::format_err,
        lazy_static::lazy_static,
        std::convert::TryFrom,
        wlan_common::{assert_variant, fake_bss_description},
        wlan_rsn::auth,
    };

    lazy_static! {
        static ref SSID: Ssid = Ssid::try_from("foo").unwrap();
    }

    #[test]
    fn test_discovery_scan_stats_lifecycle() {
        let mut stats_collector = StatsCollector::default();
        let req = fake_scan_request();
        let is_connected = true;
        assert!(stats_collector.report_discovery_scan_started(req, is_connected).is_none());

        let stats =
            stats_collector.report_discovery_scan_ended(fidl_mlme::ScanResultCode::Success, 1);
        assert_variant!(stats, Ok(scan_stats) => {
            assert!(scan_stats.scan_time().into_nanos() > 0);
            assert_eq!(scan_stats.scan_type, fidl_mlme::ScanTypes::Active);
            assert_eq!(scan_stats.scan_start_while_connected, is_connected);
            assert_eq!(scan_stats.result_code, fidl_mlme::ScanResultCode::Success);
            assert_eq!(scan_stats.bss_count, 1);
        })
    }

    #[test]
    fn test_connect_stats_lifecycle() {
        let mut stats_collector = StatsCollector::default();
        let stats = simulate_connect_lifecycle(&mut stats_collector);

        assert_variant!(stats, Ok(stats) => {
            assert!(stats.connect_time().into_nanos() > 0);
            assert!(stats.auth_time().is_some());
            assert!(stats.assoc_time().is_some());
            assert!(stats.rsna_time().is_some());
            assert_eq!(stats.result, ConnectResult::Success);
            let bss = fake_bss_description!(Wpa2, ssid: SSID.clone());
            let candidate_network = CandidateNetwork { bss, multiple_bss_candidates: true };
            assert_eq!(stats.candidate_network, Some(candidate_network));
        });
    }

    #[test]
    fn test_connect_stats_finalized_midway() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        let bss = fake_bss_description!(Wpa2, ssid: SSID.clone());
        let candidate_network = CandidateNetwork { bss, multiple_bss_candidates: true };
        assert!(stats_collector.report_candidate_network(candidate_network).is_ok());
        assert!(stats_collector.report_auth_started().is_ok());
        let result = ConnectResult::Failed(ConnectFailure::AuthenticationFailure(
            fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
        ));
        let stats = stats_collector.report_connect_finished(result.clone());

        assert_variant!(stats, Ok(stats) => {
            assert!(stats.connect_time().into_nanos() > 0);
            assert!(stats.auth_time().is_some());
            assert!(stats.assoc_time().is_none());
            assert!(stats.rsna_time().is_none());
            assert_eq!(stats.result, result);
            assert!(stats.candidate_network.is_some());
        });
    }

    #[test]
    fn test_connect_stats_establish_rsna_failure() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        // Connecting should complete other steps first before starting establish RSNA step,
        // but for testing starting with RSNA step right away is sufficient.
        assert!(stats_collector.report_rsna_started().is_ok());
        let update_sink = vec![SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished)];
        assert!(stats_collector.report_supplicant_updates(&update_sink).is_ok());
        assert!(stats_collector.report_key_exchange_timeout().is_ok());
        assert!(stats_collector.report_key_exchange_timeout().is_ok());

        assert!(stats_collector.report_supplicant_error(format_err!("blah")).is_ok());
        let stats = stats_collector.report_connect_finished(
            EstablishRsnaFailure {
                auth_method: Some(auth::MethodName::Psk),
                reason: EstablishRsnaFailureReason::OverallTimeout,
            }
            .into(),
        );

        assert_variant!(stats, Ok(stats) => {
            assert!(stats.supplicant_error.is_some());
            assert_variant!(stats.supplicant_progress, Some(SupplicantProgress {
                pmksa_established: true,
                ptksa_established: false,
                gtksa_established: false,
                esssa_established: false,
            }));
            assert_eq!(stats.num_rsna_key_frame_exchange_timeout, 2);
        });
    }

    #[test]
    fn test_consecutive_connect_attempts_stats() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        let failure1: ConnectFailure = SelectNetworkFailure::NoScanResultWithSsid.into();
        let stats = stats_collector.report_connect_finished(failure1.clone().into());
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 1);
            assert_eq!(stats.last_ten_failures, &[failure1.clone()])
        });

        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        let failure2: ConnectFailure = EstablishRsnaFailure {
            auth_method: Some(auth::MethodName::Psk),
            reason: EstablishRsnaFailureReason::OverallTimeout,
        }
        .into();
        let stats = stats_collector.report_connect_finished(failure2.clone().into());
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 2);
            assert_eq!(stats.last_ten_failures, &[failure1.clone(), failure2.clone()]);
        });

        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        let stats = stats_collector.report_connect_finished(ConnectResult::Success);
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 3);
            assert_eq!(stats.last_ten_failures, &[failure1.clone(), failure2.clone()]);
        });

        // After a successful connection, new connect attempts tracking is reset
        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        let stats = stats_collector.report_connect_finished(ConnectResult::Success);
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 1);
            assert_eq!(stats.last_ten_failures, &[])
        });
    }

    #[test]
    fn test_consecutive_connect_attempts_different_ssid_resets_stats() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        let failure1: ConnectFailure = SelectNetworkFailure::NoScanResultWithSsid.into();
        let _stats = stats_collector.report_connect_finished(failure1.clone().into());

        assert!(stats_collector.report_connect_started(Ssid::try_from("bar").unwrap()).is_none());
        let failure2: ConnectFailure = EstablishRsnaFailure {
            auth_method: Some(auth::MethodName::Psk),
            reason: EstablishRsnaFailureReason::OverallTimeout,
        }
        .into();
        let stats = stats_collector.report_connect_finished(failure2.clone().into());
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 1);
            assert_eq!(stats.last_ten_failures, &[failure2.clone()]);
        });
    }

    #[test]
    fn test_consecutive_connect_attempts_only_ten_failures_are_tracked() {
        let mut stats_collector = StatsCollector::default();
        for i in 1..=20 {
            assert!(stats_collector
                .report_connect_started(Ssid::try_from("foo").unwrap())
                .is_none());
            let stats = stats_collector
                .report_connect_finished(SelectNetworkFailure::NoScanResultWithSsid.into());
            assert_variant!(stats, Ok(stats) => {
                assert_eq!(stats.attempts, i);
                assert_eq!(stats.last_ten_failures.len(), std::cmp::min(i as usize, 10));
            });
        }
    }

    #[test]
    fn test_disconnect_then_reconnect() {
        let mut stats_collector = StatsCollector::default();
        let stats = simulate_connect_lifecycle(&mut stats_collector);
        assert_variant!(stats, Ok(stats) => stats.previous_disconnect_info.is_none());

        stats_collector.report_disconnect(
            Ssid::try_from("foo").unwrap(),
            DisconnectSource::User(fidl_sme::UserDisconnectReason::WlanSmeUnitTesting),
        );
        let stats = simulate_connect_lifecycle(&mut stats_collector);
        assert_variant!(stats, Ok(stats) => {
            assert_variant!(stats.previous_disconnect_info, Some(info) => {
                assert_eq!(info.disconnect_source, DisconnectSource::User(fidl_sme::UserDisconnectReason::WlanSmeUnitTesting));
            })
        });
    }

    #[test]
    fn test_disconnect_then_connect_fails_before_succeeding() {
        let mut stats_collector = StatsCollector::default();

        // Connects then disconnect
        let stats = simulate_connect_lifecycle(&mut stats_collector);
        assert_variant!(stats, Ok(stats) => stats.previous_disconnect_info.is_none());
        stats_collector.report_disconnect(
            Ssid::try_from("foo").unwrap(),
            DisconnectSource::User(fidl_sme::UserDisconnectReason::WlanSmeUnitTesting),
        );

        // Attempt to connect but fails
        assert!(stats_collector.report_connect_started(Ssid::try_from("foo").unwrap()).is_none());
        let failure = ConnectFailure::ScanFailure(fidl_mlme::ScanResultCode::InternalError).into();
        let stats = stats_collector.report_connect_finished(failure);

        // Previous disconnect info should not be reported on fail attempt
        assert_variant!(stats, Ok(stats) => stats.previous_disconnect_info.is_none());

        // Connect now succeeds, hence previous disconnect info is reported
        let stats = simulate_connect_lifecycle(&mut stats_collector);
        assert_variant!(stats, Ok(stats) => {
            assert_variant!(stats.previous_disconnect_info, Some(info) => {
                assert_eq!(info.disconnect_source, DisconnectSource::User(fidl_sme::UserDisconnectReason::WlanSmeUnitTesting));
            })
        });
    }

    #[test]
    fn test_no_pending_discovery_scan_stats() {
        let mut stats_collector = StatsCollector::default();
        let stats =
            stats_collector.report_discovery_scan_ended(fidl_mlme::ScanResultCode::Success, 1);
        assert_variant!(stats, Err(StatsError::NoPendingScan));
    }

    #[test]
    fn test_no_pending_connect_stats() {
        let bss = fake_bss_description!(Wpa2, ssid: SSID.clone());
        let candidate_network = CandidateNetwork { bss, multiple_bss_candidates: true };
        assert_variant!(
            StatsCollector::default().report_candidate_network(candidate_network),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::default().report_auth_started(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::default().report_assoc_started(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::default().report_assoc_success(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::default().report_rsna_started(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::default().report_rsna_established(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::default().report_connect_finished(ConnectResult::Success),
            Err(StatsError::NoPendingConnect)
        );
    }

    fn simulate_connect_lifecycle(
        stats_collector: &mut StatsCollector,
    ) -> Result<ConnectStats, StatsError> {
        assert!(stats_collector.report_connect_started(SSID.clone()).is_none());
        let bss = fake_bss_description!(Wpa2, ssid: SSID.clone());
        let candidate_network = CandidateNetwork { bss, multiple_bss_candidates: true };
        assert!(stats_collector.report_candidate_network(candidate_network).is_ok());
        assert!(stats_collector.report_auth_started().is_ok());
        assert!(stats_collector.report_assoc_started().is_ok());
        assert!(stats_collector.report_assoc_success().is_ok());
        assert!(stats_collector.report_rsna_started().is_ok());
        assert!(stats_collector.report_rsna_established().is_ok());
        stats_collector.report_connect_finished(ConnectResult::Success)
    }
}
