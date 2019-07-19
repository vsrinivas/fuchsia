// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{
            bss::{get_channel_map, get_standard_map, ClientConfig},
            scan, ConnectFailure, ConnectResult, ConnectionAttemptId, ScanTxnId, Standard,
        },
        sink::InfoSink,
    },
    failure::Fail,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::warn,
    std::collections::HashMap,
};

#[derive(Debug, PartialEq)]
pub enum InfoEvent {
    /// Sent as soon as a new connection request from the user arrives to SME
    ConnectStarted,
    /// Sent when a connection attempt succeeds, fails, or gets canceled because another connection
    /// attempt comes
    ConnectFinished { result: ConnectResult },
    /// Sent when SME forwards a ScanRequest message to MLME. Note that this may happen
    /// some time after a scan request first arrives at SME as it may be queued. If a scan
    /// request is canceled before SME sends it to MLME, this event won't be sent out
    MlmeScanStart { txn_id: ScanTxnId },
    /// Sent when SME receives a ScanEnd message from MLME (signaling that an existing scan
    /// attempt finishes)
    MlmeScanEnd { txn_id: ScanTxnId },
    /// Sent when SME finishes selecting a network and starts the Join step during a connection
    /// attempt.
    JoinStarted { att_id: ConnectionAttemptId },
    /// Sent when SME finishes the association step during a connection attempt
    AssociationSuccess { att_id: ConnectionAttemptId },
    /// Sent when SME starts the step of establishing security during a connection attempt
    RsnaStarted { att_id: ConnectionAttemptId },
    /// Sent when SME finishes the step of establishing security during a connection attempt
    RsnaEstablished { att_id: ConnectionAttemptId },
    /// Event for the aggregated stats of a discovery scan. Sent when a discovery scan has
    /// finished, as signaled by a MlmeScanEnd event
    DiscoveryScanStats(ScanStats, Option<DiscoveryStats>),
    /// Event for the aggregated stats of a connection attempt. Sent when a connection attempt
    /// has finished, whether it succeeds, fails, or gets canceled.
    ConnectStats(ConnectStats),
}

#[derive(Clone, Debug, PartialEq)]
pub enum ScanResult {
    Success,
    Failed(fidl_mlme::ScanResultCodes),
}

#[derive(Debug, PartialEq)]
pub struct ScanStats {
    pub scan_start_at: zx::Time,
    pub scan_end_at: zx::Time,
    pub scan_type: fidl_mlme::ScanTypes,
    pub scan_start_while_connected: bool,
    pub result: ScanResult,
}

impl ScanStats {
    pub fn scan_time(&self) -> zx::Duration {
        self.scan_end_at - self.scan_start_at
    }
}

#[derive(Debug, PartialEq)]
pub struct DiscoveryStats {
    pub bss_count: usize,
    pub ess_count: usize,
    pub num_bss_by_standard: HashMap<Standard, usize>,
    pub num_bss_by_channel: HashMap<u8, usize>,
}

#[derive(Debug, PartialEq)]
pub struct ConnectStats {
    pub connect_start_at: zx::Time,
    pub connect_end_at: zx::Time,

    pub scan_start_stats: Option<ScanStartStats>,
    pub scan_end_stats: Option<ScanEndStats>,

    pub auth_start_at: Option<zx::Time>,
    pub auth_end_at: Option<zx::Time>,

    pub assoc_start_at: Option<zx::Time>,
    pub assoc_end_at: Option<zx::Time>,

    pub rsna_start_at: Option<zx::Time>,
    pub rsna_end_at: Option<zx::Time>,

    pub result: ConnectResult,
    pub selected_network: Option<fidl_mlme::BssDescription>,
}

#[derive(Debug, PartialEq)]
pub struct ScanStartStats {
    pub scan_start_at: zx::Time,
    pub scan_type: fidl_mlme::ScanTypes,
    pub scan_start_while_connected: bool,
}

#[derive(Debug, PartialEq)]
pub struct ScanEndStats {
    pub scan_end_at: zx::Time,
    pub result: ScanResult,
}

impl ConnectStats {
    pub fn connect_time(&self) -> zx::Duration {
        self.connect_end_at - self.connect_start_at
    }

    pub fn join_scan_stats(&self) -> Option<ScanStats> {
        match (&self.scan_end_stats, &self.scan_start_stats) {
            (Some(end_stats), Some(start_stats)) => Some(ScanStats {
                scan_start_at: start_stats.scan_start_at,
                scan_end_at: end_stats.scan_end_at,
                scan_type: start_stats.scan_type,
                scan_start_while_connected: start_stats.scan_start_while_connected,
                result: end_stats.result.clone(),
            }),
            _ => None,
        }
    }

    pub fn auth_time(&self) -> Option<zx::Duration> {
        to_duration(&self.auth_end_at, &self.auth_start_at)
    }

    pub fn assoc_time(&self) -> Option<zx::Duration> {
        to_duration(&self.assoc_end_at, &self.assoc_start_at)
    }

    pub fn rsna_time(&self) -> Option<zx::Duration> {
        to_duration(&self.rsna_end_at, &self.rsna_start_at)
    }
}

fn to_duration(end: &Option<zx::Time>, start: &Option<zx::Time>) -> Option<zx::Duration> {
    match (end, start) {
        (Some(end), Some(start)) => Some(*end - *start),
        _ => None,
    }
}

macro_rules! warn_if_err {
    ($expr:expr) => {{
        if let Err(e) = &$expr {
            warn!("[stats] {}", e);
        };
    }};
}

pub struct InfoReporter {
    info_sink: InfoSink,
    stats_collector: StatsCollector,
}

impl InfoReporter {
    pub fn new(info_sink: InfoSink) -> Self {
        Self { info_sink, stats_collector: StatsCollector::new() }
    }

    pub fn report_scan_started(
        &mut self,
        req: fidl_mlme::ScanRequest,
        join_scan: bool,
        is_connected: bool,
    ) {
        self.info_sink.send(InfoEvent::MlmeScanStart { txn_id: req.txn_id });
        if join_scan {
            warn_if_err!(self.stats_collector.report_join_scan_started(req, is_connected));
        } else {
            if let Some(_) = self.stats_collector.report_discovery_scan_started(req, is_connected) {
                warn!("[stats] evicting unfinished discovery scan attempt");
            }
        }
    }

    pub fn report_scan_ended<D, J>(
        &mut self,
        txn_id: ScanTxnId,
        result: &scan::ScanResult<D, J>,
        cfg: &ClientConfig,
    ) {
        self.info_sink.send(InfoEvent::MlmeScanEnd { txn_id });
        match result {
            scan::ScanResult::DiscoveryFinished { result, .. } => {
                let (bss_list, scan_result) = convert_scan_result(result);
                let stats =
                    self.stats_collector.report_discovery_scan_ended(scan_result, bss_list, cfg);
                warn_if_err!(stats);
                if let Ok(stats) = stats {
                    self.info_sink.send(InfoEvent::DiscoveryScanStats(stats.0, stats.1));
                }
            }
            scan::ScanResult::JoinScanFinished { result, .. } => {
                let (_bss_list, scan_result) = convert_scan_result(result);
                // Join scan stats are collected as part of ConnectStats, which will be reported
                // when the connect attempt finishes
                warn_if_err!(self.stats_collector.report_join_scan_ended(scan_result));
            }
            scan::ScanResult::None => (),
        }
    }

    pub fn report_connect_started(&mut self) {
        self.info_sink.send(InfoEvent::ConnectStarted);
        if let Some(_prev) = self.stats_collector.report_connect_started() {
            warn!("[stats] evicting unfinished connect attempt");
        }
    }

    pub fn report_network_selected(&mut self, desc: fidl_mlme::BssDescription) {
        warn_if_err!(self.stats_collector.report_network_selected(desc));
    }

    pub fn report_join_started(&mut self, att_id: ConnectionAttemptId) {
        self.info_sink.send(InfoEvent::JoinStarted { att_id });
    }

    pub fn report_auth_started(&mut self) {
        warn_if_err!(self.stats_collector.report_auth_started());
    }

    pub fn report_assoc_started(&mut self) {
        warn_if_err!(self.stats_collector.report_assoc_started());
    }

    pub fn report_assoc_success(&mut self, att_id: ConnectionAttemptId) {
        self.info_sink.send(InfoEvent::AssociationSuccess { att_id });
        warn_if_err!(self.stats_collector.report_assoc_success());
    }

    pub fn report_rsna_started(&mut self, att_id: ConnectionAttemptId) {
        self.info_sink.send(InfoEvent::RsnaStarted { att_id });
        warn_if_err!(self.stats_collector.report_rsna_started());
    }

    pub fn report_rsna_established(&mut self, att_id: ConnectionAttemptId) {
        self.info_sink.send(InfoEvent::RsnaEstablished { att_id });
        warn_if_err!(self.stats_collector.report_rsna_established());
    }

    pub fn report_connect_finished(&mut self, result: ConnectResult) {
        self.info_sink.send(InfoEvent::ConnectFinished { result: result.clone() });
        let stats = self.stats_collector.report_connect_finished(result);
        warn_if_err!(stats);
        if let Ok(stats) = stats {
            self.info_sink.send(InfoEvent::ConnectStats(stats));
        }
    }
}

#[derive(Debug, Fail)]
pub enum StatsError {
    #[fail(display = "no current pending connect")]
    NoPendingConnect,
    #[fail(display = "no current pending scan")]
    NoPendingScan,
}

pub struct StatsCollector {
    discovery_scan_stats: Option<PendingScanStats>,
    connect_stats: Option<PendingConnectStats>,
}

impl StatsCollector {
    pub fn new() -> Self {
        Self { discovery_scan_stats: None, connect_stats: None }
    }

    pub fn report_join_scan_started(
        &mut self,
        req: fidl_mlme::ScanRequest,
        is_connected: bool,
    ) -> Result<(), StatsError> {
        let now = now();
        let pending_scan_stats =
            PendingScanStats { scan_start_at: now, req, scan_start_while_connected: is_connected };
        let stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        stats.pending_scan_stats.replace(pending_scan_stats);
        Ok(())
    }

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
        result: ScanResult,
        bss_list: Option<&Vec<fidl_mlme::BssDescription>>,
        cfg: &ClientConfig,
    ) -> Result<(ScanStats, Option<DiscoveryStats>), StatsError> {
        let now = now();
        let pending_stats = self.discovery_scan_stats.take().ok_or(StatsError::NoPendingScan)?;

        let scan_stats = ScanStats {
            scan_type: pending_stats.req.scan_type,
            scan_start_while_connected: pending_stats.scan_start_while_connected,
            scan_start_at: pending_stats.scan_start_at,
            scan_end_at: now,
            result,
        };
        let discovery_stats = bss_list.map(|bss_list| {
            let bss_count = bss_list.len();
            let ess_count = cfg.group_networks(&bss_list).len();
            let num_bss_by_standard = get_standard_map(&bss_list);
            let num_bss_by_channel = get_channel_map(&bss_list);

            DiscoveryStats { bss_count, ess_count, num_bss_by_standard, num_bss_by_channel }
        });

        Ok((scan_stats, discovery_stats))
    }

    pub fn report_join_scan_ended(&mut self, result: ScanResult) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        pending_stats.scan_end_stats.replace(ScanEndStats { scan_end_at: now(), result });
        Ok(())
    }

    pub fn report_connect_started(&mut self) -> Option<PendingConnectStats> {
        self.connect_stats.replace(PendingConnectStats::new(now()))
    }

    pub fn report_network_selected(
        &mut self,
        desc: fidl_mlme::BssDescription,
    ) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        pending_stats.selected_network.replace(desc);
        Ok(())
    }

    pub fn report_auth_started(&mut self) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        pending_stats.auth_start_at.replace(now());
        Ok(())
    }

    pub fn report_assoc_started(&mut self) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        let now = now();
        pending_stats.auth_end_at.replace(now);
        pending_stats.assoc_start_at.replace(now);
        Ok(())
    }

    pub fn report_assoc_success(&mut self) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        pending_stats.assoc_end_at.replace(now());
        Ok(())
    }

    pub fn report_rsna_started(&mut self) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        pending_stats.rsna_start_at.replace(now());
        Ok(())
    }

    pub fn report_rsna_established(&mut self) -> Result<(), StatsError> {
        let pending_stats = self.connect_stats.as_mut().ok_or(StatsError::NoPendingConnect)?;
        pending_stats.rsna_end_at.replace(now());
        Ok(())
    }

    pub fn report_connect_finished(
        &mut self,
        result: ConnectResult,
    ) -> Result<ConnectStats, StatsError> {
        let pending_stats = self.connect_stats.take().ok_or(StatsError::NoPendingConnect)?;
        Ok(self.finalize_connect_stats(pending_stats, result))
    }

    fn finalize_connect_stats(
        &mut self,
        mut pending_stats: PendingConnectStats,
        result: ConnectResult,
    ) -> ConnectStats {
        let now = now();
        self.fill_end_result(&mut pending_stats, &result, now);

        ConnectStats {
            connect_start_at: pending_stats.connect_start_at,
            connect_end_at: now,
            scan_start_stats: pending_stats.pending_scan_stats.map(|stats| ScanStartStats {
                scan_start_at: stats.scan_start_at,
                scan_type: stats.req.scan_type,
                scan_start_while_connected: stats.scan_start_while_connected,
            }),
            scan_end_stats: pending_stats.scan_end_stats,
            auth_start_at: pending_stats.auth_start_at,
            auth_end_at: pending_stats.auth_end_at,
            assoc_start_at: pending_stats.assoc_start_at,
            assoc_end_at: pending_stats.assoc_end_at,
            rsna_start_at: pending_stats.rsna_start_at,
            rsna_end_at: pending_stats.rsna_end_at,
            result,
            selected_network: pending_stats.selected_network,
        }
    }

    fn fill_end_result(
        &mut self,
        pending_stats: &mut PendingConnectStats,
        result: &ConnectResult,
        now: zx::Time,
    ) {
        match result {
            ConnectResult::Failed(failure) => match failure {
                ConnectFailure::ScanFailure(code) => {
                    pending_stats.scan_end_stats.replace(ScanEndStats {
                        scan_end_at: now,
                        result: ScanResult::Failed(*code),
                    });
                }
                ConnectFailure::AuthenticationFailure(..) => {
                    pending_stats.auth_end_at.replace(now);
                }
                ConnectFailure::AssociationFailure(..) => {
                    pending_stats.assoc_end_at.replace(now);
                }
                ConnectFailure::RsnaTimeout | ConnectFailure::EstablishRsna => {
                    pending_stats.rsna_end_at.replace(now);
                }
                _ => (),
            },
            _ => (),
        }
    }
}

fn convert_scan_result(
    result: &Result<Vec<fidl_mlme::BssDescription>, fidl_mlme::ScanResultCodes>,
) -> (Option<&Vec<fidl_mlme::BssDescription>>, ScanResult) {
    match result {
        Ok(bss_list) => (Some(bss_list), ScanResult::Success),
        Err(code) => (None, ScanResult::Failed(*code)),
    }
}

fn now() -> zx::Time {
    zx::Time::get(zx::ClockId::Monotonic)
}

pub struct PendingScanStats {
    scan_start_at: zx::Time,
    req: fidl_mlme::ScanRequest,
    scan_start_while_connected: bool,
}

pub struct PendingConnectStats {
    connect_start_at: zx::Time,
    pending_scan_stats: Option<PendingScanStats>,
    scan_end_stats: Option<ScanEndStats>,
    auth_start_at: Option<zx::Time>,
    auth_end_at: Option<zx::Time>,
    assoc_start_at: Option<zx::Time>,
    assoc_end_at: Option<zx::Time>,
    rsna_start_at: Option<zx::Time>,
    rsna_end_at: Option<zx::Time>,
    selected_network: Option<fidl_mlme::BssDescription>,
}

impl PendingConnectStats {
    fn new(connect_start_at: zx::Time) -> Self {
        Self {
            connect_start_at,
            pending_scan_stats: None,
            scan_end_stats: None,
            auth_start_at: None,
            auth_end_at: None,
            assoc_start_at: None,
            assoc_end_at: None,
            rsna_start_at: None,
            rsna_end_at: None,
            selected_network: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::client::test_utils::{fake_protected_bss_description, fake_scan_request};

    use maplit::hashmap;
    use wlan_common::assert_variant;

    #[test]
    fn test_discovery_scan_stats_lifecycle() {
        let mut stats_collector = StatsCollector::new();
        let req = fake_scan_request();
        let is_connected = true;
        assert!(stats_collector.report_discovery_scan_started(req, is_connected).is_none());

        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        let cfg = ClientConfig::default();
        let stats = stats_collector.report_discovery_scan_ended(
            ScanResult::Success,
            Some(&vec![bss_desc]),
            &cfg,
        );
        assert_variant!(stats, Ok((scan_stats, discovery_stats)) => {
            assert!(scan_stats.scan_time().into_nanos() > 0);
            assert_eq!(scan_stats.scan_type, fidl_mlme::ScanTypes::Active);
            assert_eq!(scan_stats.scan_start_while_connected, is_connected);
            assert_eq!(scan_stats.result, ScanResult::Success);
            assert_eq!(discovery_stats, Some(DiscoveryStats {
                bss_count: 1,
                ess_count: 1,
                num_bss_by_channel: hashmap! { 1 => 1 },
                num_bss_by_standard: hashmap! { Standard::G => 1 },
            }));
        })
    }

    #[test]
    fn test_connect_stats_lifecycle() {
        let mut stats_collector = StatsCollector::new();

        assert!(stats_collector.report_connect_started().is_none());
        let scan_req = fake_scan_request();
        let is_connected = false;
        assert!(stats_collector.report_join_scan_started(scan_req, is_connected).is_ok());
        assert!(stats_collector.report_join_scan_ended(ScanResult::Success).is_ok());
        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        assert!(stats_collector.report_network_selected(bss_desc).is_ok());
        assert!(stats_collector.report_auth_started().is_ok());
        assert!(stats_collector.report_assoc_started().is_ok());
        assert!(stats_collector.report_assoc_success().is_ok());
        assert!(stats_collector.report_rsna_started().is_ok());
        assert!(stats_collector.report_rsna_established().is_ok());
        let stats = stats_collector.report_connect_finished(ConnectResult::Success);

        assert_variant!(stats, Ok(stats) => {
            assert!(stats.connect_time().into_nanos() > 0);
            assert_variant!(stats.join_scan_stats(), Some(scan_stats) => {
                assert!(scan_stats.scan_time().into_nanos() > 0);
                assert_eq!(scan_stats.scan_type, fidl_mlme::ScanTypes::Active);
                assert_eq!(scan_stats.scan_start_while_connected, is_connected);
                assert_eq!(scan_stats.result, ScanResult::Success);
            });
            assert!(stats.auth_time().is_some());
            assert!(stats.assoc_time().is_some());
            assert!(stats.rsna_time().is_some());
            assert_eq!(stats.result, ConnectResult::Success);
            let bss_desc = fake_protected_bss_description(b"foo".to_vec());
            assert_eq!(stats.selected_network, Some(bss_desc));
        });
    }

    #[test]
    fn test_connect_stats_finalized_midway() {
        let mut stats_collector = StatsCollector::new();

        assert!(stats_collector.report_connect_started().is_none());
        let scan_req = fake_scan_request();
        let is_connected = false;
        assert!(stats_collector.report_join_scan_started(scan_req, is_connected).is_ok());
        assert!(stats_collector.report_join_scan_ended(ScanResult::Success).is_ok());
        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        assert!(stats_collector.report_network_selected(bss_desc).is_ok());
        assert!(stats_collector.report_auth_started().is_ok());
        let result = ConnectResult::Failed(ConnectFailure::AuthenticationFailure(
            fidl_mlme::AuthenticateResultCodes::Refused,
        ));
        let stats = stats_collector.report_connect_finished(result.clone());

        assert_variant!(stats, Ok(stats) => {
            assert!(stats.connect_time().into_nanos() > 0);
            assert!(stats.join_scan_stats().is_some());
            assert!(stats.auth_time().is_some());
            assert!(stats.assoc_time().is_none());
            assert!(stats.rsna_time().is_none());
            assert_eq!(stats.result, result);
            assert!(stats.selected_network.is_some());
        });
    }

    #[test]
    fn test_no_pending_discovery_scan_stats() {
        let mut stats_collector = StatsCollector::new();
        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        let cfg = ClientConfig::default();
        let stats = stats_collector.report_discovery_scan_ended(
            ScanResult::Success,
            Some(&vec![bss_desc]),
            &cfg,
        );
        assert_variant!(stats, Err(StatsError::NoPendingScan));
    }

    #[test]
    fn test_no_pending_connect_stats() {
        assert_variant!(
            StatsCollector::new().report_join_scan_ended(ScanResult::Success),
            Err(StatsError::NoPendingConnect)
        );
        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        assert_variant!(
            StatsCollector::new().report_network_selected(bss_desc),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::new().report_auth_started(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::new().report_assoc_started(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::new().report_assoc_success(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::new().report_rsna_started(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::new().report_rsna_established(),
            Err(StatsError::NoPendingConnect)
        );
        assert_variant!(
            StatsCollector::new().report_connect_finished(ConnectResult::Success),
            Err(StatsError::NoPendingConnect)
        );
    }
}
