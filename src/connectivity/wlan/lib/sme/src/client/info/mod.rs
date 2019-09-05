// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod info_reporter;
mod stats_collector;

pub(crate) use {
    self::{
        info_reporter::InfoReporter,
        stats_collector::StatsCollector,
    },
};

use {
    crate::{
        client::{
            ConnectFailure, ConnectResult, ConnectionAttemptId, ScanTxnId,
        },
        Ssid,
    },
    derivative::Derivative,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon::{self as zx, prelude::DurationNum},
    std::collections::HashMap,
    wlan_common::bss::Standard,
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

#[derive(Derivative)]
#[derivative(Debug, PartialEq)]
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

    /// Possible detailed error from supplicant. May be downcast to wlan_rsn::Error.
    #[derivative(PartialEq(compare_with = "cmp_supplicant_error"))]
    pub supplicant_error: Option<failure::Error>,
    pub supplicant_progress: Option<SupplicantProgress>,

    /// Total number of times timeout triggers during all RSNA key frame exchanges for this
    /// connection.
    pub num_rsna_key_frame_exchange_timeout: u32,

    /// Number of consecutive connection attempts that have been made to the same SSID.
    pub attempts: u32,
    /// Failures seen trying to connect the same SSID. Only up to ten failures are tracked. If
    /// the latest connection result is a failure, it's also included in this list.
    ///
    /// Note that this only tracks consecutive attempts to the same SSID. This means that
    /// connection attempt to another SSID would clear out previous history.
    pub last_ten_failures: Vec<ConnectFailure>,

    /// Information about previous disconnection. Only included when connection attempt
    /// succeeds and previous disconnect was manual or due to a connection drop. That is,
    /// this field is not included if SME disconnects in order to fulfill a connection
    /// attempt.
    ///
    /// Intended to be used for client to compute time it takes from when user last
    /// disconnects to when client is reconnected.
    pub previous_disconnect_info: Option<PreviousDisconnectInfo>,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct SupplicantProgress {
    pub pmksa_established: bool,
    pub ptksa_established: bool,
    pub gtksa_established: bool,
    pub esssa_established: bool,
}

fn cmp_supplicant_error(left: &Option<failure::Error>, right: &Option<failure::Error>) -> bool {
    match (left, right) {
        (Some(e1), Some(e2)) => format!("{:?}", e1) == format!("{:?}", e2),
        (None, None) => true,
        _ => false,
    }
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

#[derive(Debug, Clone, PartialEq)]
pub struct PreviousDisconnectInfo {
    pub ssid: Ssid,
    pub disconnect_cause: DisconnectCause,
    pub disconnect_at: zx::Time,
}

#[derive(Debug, Clone, PartialEq)]
pub enum DisconnectCause {
    /// Disconnect happens due to manual request.
    Manual,
    /// Disconnect happens due to deauth or diassociate indication from MLME.
    Drop,
}