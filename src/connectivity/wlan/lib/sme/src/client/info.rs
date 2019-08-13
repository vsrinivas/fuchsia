// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{
            bss::ClientConfig, scan, ConnectFailure, ConnectResult, ConnectionAttemptId, ScanTxnId,
        },
        sink::InfoSink,
        Ssid,
    },
    failure::Fail,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon::{self as zx, prelude::DurationNum},
    log::warn,
    std::collections::{HashMap, VecDeque},
    wlan_common::bss::{get_channel_map, get_phy_standard_map, Standard},
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
    /// Event generated whenever the client first connects, or when the connection has reached
    /// a certain milestone (e.g. 1/10/30 minutes)
    ConnectionMilestone(ConnectionMilestoneInfo),
    /// Event generated whenever SME receives a deauth or disassoc indication while connected
    ConnectionLost(ConnectionLostInfo),
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
    /// Number of BSS found in scan result. For join scan, this only counts BSS with requested
    /// SSID.
    pub bss_count: usize,
}

impl ScanStats {
    pub fn scan_time(&self) -> zx::Duration {
        self.scan_end_at - self.scan_start_at
    }
}

#[derive(Debug, PartialEq)]
pub struct DiscoveryStats {
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
    pub candidate_network: Option<fidl_mlme::BssDescription>,

    /// Number of consecutive connection attempts that have been made to the same SSID
    pub attempts: u32,
    /// Failures seen trying to connect the same SSID. Only up to ten failures are tracked. If
    /// the latest connection result is a failure, it's also included in this list.
    ///
    /// Note that this only tracks consecutive attempts to the same SSID. This means that
    /// connection attempt to another SSID would clear out previous history.
    pub last_ten_failures: Vec<ConnectFailure>,
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
    /// Number of BSS found with the requested SSID from join scan
    pub bss_count: usize,
}

impl ConnectStats {
    pub fn connect_time(&self) -> zx::Duration {
        self.connect_end_at - self.connect_start_at
    }

    /// Time from when SME receives connect request to when it starts servicing that request
    /// (i.e. when join scan starts). This can happen when SME waits for existing scan to
    /// finish before it can start a join scan.
    ///
    /// If connect request is canceled before it's serviced, return None.
    pub fn connect_queued_time(&self) -> Option<zx::Duration> {
        self.scan_start_stats.as_ref().map(|stats| stats.scan_start_at - self.connect_start_at)
    }

    pub fn connect_time_without_scan(&self) -> Option<zx::Duration> {
        self.scan_end_stats.as_ref().map(|stats| self.connect_end_at - stats.scan_end_at)
    }

    pub fn join_scan_stats(&self) -> Option<ScanStats> {
        match (&self.scan_end_stats, &self.scan_start_stats) {
            (Some(end_stats), Some(start_stats)) => Some(ScanStats {
                scan_start_at: start_stats.scan_start_at,
                scan_end_at: end_stats.scan_end_at,
                scan_type: start_stats.scan_type,
                scan_start_while_connected: start_stats.scan_start_while_connected,
                result: end_stats.result.clone(),
                bss_count: end_stats.bss_count,
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

#[derive(Debug, Clone, PartialEq)]
pub struct ConnectionMilestoneInfo {
    pub milestone: ConnectionMilestone,
    pub connected_since: zx::Time,
}

impl ConnectionMilestoneInfo {
    pub fn new(milestone: ConnectionMilestone, connected_since: zx::Time) -> Self {
        Self { connected_since, milestone }
    }

    pub fn next_milestone(&self) -> Option<Self> {
        self.milestone
            .next_milestone()
            .map(|milestone| ConnectionMilestoneInfo::new(milestone, self.connected_since))
    }

    /// Return the earliest that at which this connection milestone would be satisfied
    pub fn deadline(&self) -> zx::Time {
        self.connected_since + self.milestone.duration()
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum ConnectionMilestone {
    Connected,
    OneMinute,
    TenMinutes,
    ThirtyMinutes,
    OneHour,
    ThreeHours,
    SixHours,
    TwelveHours,
    OneDay,
}

impl ConnectionMilestone {
    /// Given the current connection milestone, return the next milestone. If current milestone
    /// is already the highest that's tracked, return None.
    pub fn next_milestone(&self) -> Option<Self> {
        match self {
            ConnectionMilestone::Connected => Some(ConnectionMilestone::OneMinute),
            ConnectionMilestone::OneMinute => Some(ConnectionMilestone::TenMinutes),
            ConnectionMilestone::TenMinutes => Some(ConnectionMilestone::ThirtyMinutes),
            ConnectionMilestone::ThirtyMinutes => Some(ConnectionMilestone::OneHour),
            ConnectionMilestone::OneHour => Some(ConnectionMilestone::ThreeHours),
            ConnectionMilestone::ThreeHours => Some(ConnectionMilestone::SixHours),
            ConnectionMilestone::SixHours => Some(ConnectionMilestone::TwelveHours),
            ConnectionMilestone::TwelveHours => Some(ConnectionMilestone::OneDay),
            ConnectionMilestone::OneDay => None,
        }
    }

    /// Return the minimum connected duration required to satisfy current milestone.
    pub fn duration(&self) -> zx::Duration {
        match self {
            ConnectionMilestone::Connected => 0.millis(),
            ConnectionMilestone::OneMinute => 1.minutes(),
            ConnectionMilestone::TenMinutes => 10.minutes(),
            ConnectionMilestone::ThirtyMinutes => 30.minutes(),
            ConnectionMilestone::OneHour => 1.hour(),
            ConnectionMilestone::ThreeHours => 3.hours(),
            ConnectionMilestone::SixHours => 6.hours(),
            ConnectionMilestone::TwelveHours => 12.hours(),
            ConnectionMilestone::OneDay => 24.hours(),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ConnectionLostInfo {
    pub connected_duration: zx::Duration,
    pub last_rssi: i8,
    pub bssid: [u8; 6],
}

macro_rules! warn_if_err {
    ($expr:expr) => {{
        if let Err(e) = &$expr {
            warn!("[stats] {}", e);
        };
    }};
}

pub(crate) struct InfoReporter {
    info_sink: InfoSink,
    stats_collector: StatsCollector,
}

impl InfoReporter {
    pub fn new(info_sink: InfoSink) -> Self {
        Self { info_sink, stats_collector: StatsCollector::default() }
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
                let (bss_list, scan_result) = convert_scan_result(result);
                // Join scan stats are collected as part of ConnectStats, which will be reported
                // when the connect attempt finishes
                let bss_count = bss_list.map(|bss_list| bss_list.len()).unwrap_or(0);
                warn_if_err!(self.stats_collector.report_join_scan_ended(scan_result, bss_count));
            }
            scan::ScanResult::None => (),
        }
    }

    pub fn report_connect_started(&mut self, ssid: Ssid) {
        self.info_sink.send(InfoEvent::ConnectStarted);
        if let Some(_prev) = self.stats_collector.report_connect_started(ssid) {
            warn!("[stats] evicting unfinished connect attempt");
        }
    }

    pub fn report_candidate_network(&mut self, desc: fidl_mlme::BssDescription) {
        warn_if_err!(self.stats_collector.report_candidate_network(desc));
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

    pub fn report_connection_milestone(&mut self, milestone: ConnectionMilestoneInfo) {
        self.info_sink.send(InfoEvent::ConnectionMilestone(milestone));
    }

    pub fn report_connection_lost(
        &mut self,
        connected_duration: zx::Duration,
        last_rssi: i8,
        bssid: [u8; 6],
    ) {
        self.info_sink.send(InfoEvent::ConnectionLost(ConnectionLostInfo {
            connected_duration,
            last_rssi,
            bssid,
        }));
    }
}

#[derive(Debug, Fail)]
pub(crate) enum StatsError {
    #[fail(display = "no current pending connect")]
    NoPendingConnect,
    #[fail(display = "no current pending scan")]
    NoPendingScan,
}

#[derive(Default)]
pub(crate) struct StatsCollector {
    discovery_scan_stats: Option<PendingScanStats>,
    /// Track successive connect attempts to the same SSID. This resets when attempt succeeds
    /// or when attempting to connect to a different SSID from a previous attempt.
    connect_attempts: Option<ConnectAttempts>,
}

impl StatsCollector {
    pub fn report_join_scan_started(
        &mut self,
        req: fidl_mlme::ScanRequest,
        is_connected: bool,
    ) -> Result<(), StatsError> {
        let now = now();
        let pending_scan_stats =
            PendingScanStats { scan_start_at: now, req, scan_start_while_connected: is_connected };
        self.connect_stats()?.pending_scan_stats.replace(pending_scan_stats);
        Ok(())
    }

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
            bss_count: bss_list.map(|bss_list| bss_list.len()).unwrap_or(0),
        };
        let discovery_stats = bss_list.map(|bss_list| {
            let ess_count = cfg.group_networks(&bss_list).len();
            let num_bss_by_standard = get_phy_standard_map(&bss_list);
            let num_bss_by_channel = get_channel_map(&bss_list);

            DiscoveryStats { ess_count, num_bss_by_standard, num_bss_by_channel }
        });

        Ok((scan_stats, discovery_stats))
    }

    pub fn report_join_scan_ended(
        &mut self,
        result: ScanResult,
        bss_count: usize,
    ) -> Result<(), StatsError> {
        let stats = ScanEndStats { scan_end_at: now(), result, bss_count };
        self.connect_stats()?.scan_end_stats.replace(stats);
        Ok(())
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
        desc: fidl_mlme::BssDescription,
    ) -> Result<(), StatsError> {
        self.connect_stats()?.candidate_network.replace(desc);
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

        Ok(ConnectStats {
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
            candidate_network: pending_stats.candidate_network,
            attempts: connect_attempts.attempts,
            last_ten_failures: connect_attempts.last_ten_failures.iter().cloned().collect(),
        })
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
                ConnectFailure::ScanFailure(code) => {
                    pending_stats.scan_end_stats.replace(ScanEndStats {
                        scan_end_at: now,
                        result: ScanResult::Failed(*code),
                        bss_count: 0,
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

        if let ConnectResult::Failed(failure) = result {
            if self.last_ten_failures.len() >= Self::MAX_FAILURES_TRACKED {
                self.last_ten_failures.pop_front();
            }
            self.last_ten_failures.push_back(failure.clone());
        }

        Ok(pending_stats)
    }
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
    candidate_network: Option<fidl_mlme::BssDescription>,
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
            candidate_network: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::client::{
            test_utils::{fake_bss_with_rates, fake_protected_bss_description, fake_scan_request},
            SelectNetworkFailure,
        },
        maplit::hashmap,
        wlan_common::assert_variant,
    };

    #[test]
    fn test_discovery_scan_stats_lifecycle() {
        let mut stats_collector = StatsCollector::default();
        let req = fake_scan_request();
        let is_connected = true;
        assert!(stats_collector.report_discovery_scan_started(req, is_connected).is_none());

        let bss_desc = fake_bss_with_rates(b"foo".to_vec(), vec![12]);
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
            assert_eq!(scan_stats.bss_count, 1);
            assert_eq!(discovery_stats, Some(DiscoveryStats {
                ess_count: 1,
                num_bss_by_channel: hashmap! { 1 => 1 },
                num_bss_by_standard: hashmap! { Standard::Dot11G => 1 },
            }));
        })
    }

    #[test]
    fn test_connect_stats_lifecycle() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
        let scan_req = fake_scan_request();
        let is_connected = false;
        assert!(stats_collector.report_join_scan_started(scan_req, is_connected).is_ok());
        assert!(stats_collector.report_join_scan_ended(ScanResult::Success, 1).is_ok());
        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        assert!(stats_collector.report_candidate_network(bss_desc).is_ok());
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
                assert_eq!(scan_stats.bss_count, 1);
            });
            assert!(stats.auth_time().is_some());
            assert!(stats.assoc_time().is_some());
            assert!(stats.rsna_time().is_some());
            assert_eq!(stats.result, ConnectResult::Success);
            let bss_desc = fake_protected_bss_description(b"foo".to_vec());
            assert_eq!(stats.candidate_network, Some(bss_desc));
        });
    }

    #[test]
    fn test_connect_stats_finalized_midway() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
        let scan_req = fake_scan_request();
        let is_connected = false;
        assert!(stats_collector.report_join_scan_started(scan_req, is_connected).is_ok());
        assert!(stats_collector.report_join_scan_ended(ScanResult::Success, 1).is_ok());
        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        assert!(stats_collector.report_candidate_network(bss_desc).is_ok());
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
            assert!(stats.candidate_network.is_some());
        });
    }

    #[test]
    fn test_consecutive_connect_attempts_stats() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
        let failure1: ConnectFailure = SelectNetworkFailure::NoScanResultWithSsid.into();
        let stats = stats_collector.report_connect_finished(failure1.clone().into());
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 1);
            assert_eq!(stats.last_ten_failures, &[failure1.clone()])
        });

        assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
        let stats = stats_collector
            .report_connect_finished(ConnectResult::Failed(ConnectFailure::EstablishRsna));
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 2);
            assert_eq!(stats.last_ten_failures, &[failure1.clone(), ConnectFailure::EstablishRsna]);
        });

        assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
        let stats = stats_collector.report_connect_finished(ConnectResult::Success);
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 3);
            assert_eq!(stats.last_ten_failures, &[failure1.clone(), ConnectFailure::EstablishRsna]);
        });

        // After a successful connection, new connect attempts tracking is reset
        assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
        let stats = stats_collector.report_connect_finished(ConnectResult::Success);
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 1);
            assert_eq!(stats.last_ten_failures, &[])
        });
    }

    #[test]
    fn test_consecutive_connect_attempts_different_ssid_resets_stats() {
        let mut stats_collector = StatsCollector::default();

        assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
        let failure1: ConnectFailure = SelectNetworkFailure::NoScanResultWithSsid.into();
        let _stats = stats_collector.report_connect_finished(failure1.clone().into());

        assert!(stats_collector.report_connect_started(b"bar".to_vec()).is_none());
        let stats = stats_collector
            .report_connect_finished(ConnectResult::Failed(ConnectFailure::EstablishRsna));
        assert_variant!(stats, Ok(stats) => {
            assert_eq!(stats.attempts, 1);
            assert_eq!(stats.last_ten_failures, &[ConnectFailure::EstablishRsna]);
        });
    }

    #[test]
    fn test_consecutive_connect_attempts_only_ten_failures_are_tracked() {
        let mut stats_collector = StatsCollector::default();
        for i in 1..=20 {
            assert!(stats_collector.report_connect_started(b"foo".to_vec()).is_none());
            let stats = stats_collector
                .report_connect_finished(SelectNetworkFailure::NoScanResultWithSsid.into());
            assert_variant!(stats, Ok(stats) => {
                assert_eq!(stats.attempts, i);
                assert_eq!(stats.last_ten_failures.len(), std::cmp::min(i as usize, 10));
            });
        }
    }

    #[test]
    fn test_no_pending_discovery_scan_stats() {
        let mut stats_collector = StatsCollector::default();
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
            StatsCollector::default().report_join_scan_ended(ScanResult::Success, 1),
            Err(StatsError::NoPendingConnect)
        );
        let bss_desc = fake_protected_bss_description(b"foo".to_vec());
        assert_variant!(
            StatsCollector::default().report_candidate_network(bss_desc),
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

    #[test]
    fn test_connection_milestone() {
        let start = zx::Time::from_nanos(3);
        use ConnectionMilestone::*;

        let milestone = ConnectionMilestoneInfo::new(ConnectionMilestone::Connected, start);
        let milestone = expect_next_milestone(milestone, OneMinute, 60_000000003);
        let milestone = expect_next_milestone(milestone, TenMinutes, 600_000000003);
        let milestone = expect_next_milestone(milestone, ThirtyMinutes, 1_800_000000003);
        let milestone = expect_next_milestone(milestone, OneHour, 3_600_000000003);
        let milestone = expect_next_milestone(milestone, ThreeHours, 10_800_000000003);
        let milestone = expect_next_milestone(milestone, SixHours, 21_600_000000003);
        let milestone = expect_next_milestone(milestone, TwelveHours, 43_200_000000003);
        let milestone = expect_next_milestone(milestone, OneDay, 86_400_000000003);
        assert_eq!(milestone.next_milestone(), None);
    }

    fn expect_next_milestone(
        milestone: ConnectionMilestoneInfo,
        expected_next_milestone: ConnectionMilestone,
        nanos_deadline: i64,
    ) -> ConnectionMilestoneInfo {
        let next_milestone = milestone.next_milestone();
        assert_variant!(next_milestone, Some(next_milestone) => {
            assert_eq!(next_milestone.milestone, expected_next_milestone);
            assert_eq!(next_milestone.deadline().into_nanos(), nanos_deadline);
            next_milestone
        })
    }
}
