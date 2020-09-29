// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod info_reporter;
mod stats_collector;

pub(crate) use self::{info_reporter::InfoReporter, stats_collector::StatsCollector};

use {
    crate::{
        client::{
            event::{self, Event},
            ConnectFailure, ConnectResult,
        },
        timer::TimeoutDuration,
        Ssid,
    },
    derivative::Derivative,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon::{self as zx, prelude::DurationNum},
};

#[derive(Debug, PartialEq)]
pub enum InfoEvent {
    DiscoveryScanStats(ScanStats),
    /// Event for the aggregated stats of a connection attempt. Sent when a connection attempt
    /// has finished, whether it succeeds, fails, or gets canceled.
    ConnectStats(ConnectStats),
    /// Event generated whenever the client first connects or periodically while the client
    /// remains connected
    ConnectionPing(ConnectionPingInfo),
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
    pub supplicant_error: Option<anyhow::Error>,
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

fn cmp_supplicant_error(left: &Option<anyhow::Error>, right: &Option<anyhow::Error>) -> bool {
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
pub struct ConnectionPingInfo {
    /// Start time of connected session.
    pub connected_since: zx::Time,
    /// Time when last ping was reported. This is None if this is the first ping since client
    /// is connected.
    pub last_reported: Option<zx::Time>,
    /// Time of this ping.
    pub now: zx::Time,
}
impl From<ConnectionPingInfo> for Event {
    fn from(info: ConnectionPingInfo) -> Self {
        Event::ConnectionPing(info)
    }
}
impl TimeoutDuration for ConnectionPingInfo {
    // Things to consider when setting timeout for ConnectionPingInfo:
    //
    // ConnectionPingInfo is used to log both the connection milestone (connection count by
    // duration) and connection ping metrics. The timeout should be set low enough so that none
    // of the milestone is missed. This is set at one minute because the first milestone
    // (connected one-minute) happens one minute after connected, and this is the smallest
    // granularity. To keep it simple, we just use one minute.
    //
    // Another thing to consider is that how often ConnectionPingInfo is sent affects how often
    // the connection ping metric is logged. In the case where device is offline, observations
    // are kept locally, but there's a limit on how much space unsent observations can occupy.
    // Because the ping metric only uses locally-aggregated report, which is space efficient,
    // it's okay if we send out ping often. If this situation changes, consider using a more
    // flexible timeout (for example, send a ping every hour after the first hour, with
    // consideration on whether to handle day change as well).
    fn timeout_duration(&self) -> zx::Duration {
        event::CONNECTION_PING_TIMEOUT_MINUTES.minutes()
    }
}

impl ConnectionPingInfo {
    /// Construct a ping when first connected.
    pub fn first_connected(now: zx::Time) -> Self {
        Self { connected_since: now, last_reported: None, now }
    }

    /// Construct a new ping, treating self as a previously reported ping.
    pub fn next_ping(self, now: zx::Time) -> Self {
        Self { connected_since: self.connected_since, last_reported: Some(self.now), now }
    }

    pub fn connected_duration(&self) -> zx::Duration {
        self.now - self.connected_since
    }

    /// Return the highest connection milestone that this ping satisfies.
    pub fn get_milestone(&self) -> ConnectionMilestone {
        ConnectionMilestone::from_duration(self.connected_duration())
    }

    /// Return whether this ping reaches a new connection milestone compared to the last reported
    /// ping.
    pub fn reaches_new_milestone(&self) -> bool {
        match self.last_reported {
            Some(last) => {
                let last_milestone =
                    ConnectionMilestone::from_duration(last - self.connected_since);
                self.get_milestone() != last_milestone
            }
            None => true,
        }
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
    TwoDays,
    ThreeDays,
}

impl ConnectionMilestone {
    pub fn all() -> &'static [Self] {
        &[
            ConnectionMilestone::Connected,
            ConnectionMilestone::OneMinute,
            ConnectionMilestone::TenMinutes,
            ConnectionMilestone::ThirtyMinutes,
            ConnectionMilestone::OneHour,
            ConnectionMilestone::ThreeHours,
            ConnectionMilestone::SixHours,
            ConnectionMilestone::TwelveHours,
            ConnectionMilestone::OneDay,
            ConnectionMilestone::TwoDays,
            ConnectionMilestone::ThreeDays,
        ]
    }

    /// Return the highest milestone that duration satisfies
    pub fn from_duration(duration: zx::Duration) -> Self {
        match duration {
            x if x >= 72.hours() => ConnectionMilestone::ThreeDays,
            x if x >= 48.hours() => ConnectionMilestone::TwoDays,
            x if x >= 24.hours() => ConnectionMilestone::OneDay,
            x if x >= 12.hours() => ConnectionMilestone::TwelveHours,
            x if x >= 6.hours() => ConnectionMilestone::SixHours,
            x if x >= 3.hours() => ConnectionMilestone::ThreeHours,
            x if x >= 1.hour() => ConnectionMilestone::OneHour,
            x if x >= 30.minutes() => ConnectionMilestone::ThirtyMinutes,
            x if x >= 10.minutes() => ConnectionMilestone::TenMinutes,
            x if x >= 1.minute() => ConnectionMilestone::OneMinute,
            _ => ConnectionMilestone::Connected,
        }
    }

    /// Return the minimum connected duration required to satisfy current milestone.
    pub fn min_duration(&self) -> zx::Duration {
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
            ConnectionMilestone::TwoDays => 48.hours(),
            ConnectionMilestone::ThreeDays => 72.hours(),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_whether_ping_reaches_new_milestone() {
        let start = zx::Time::get(zx::ClockId::Monotonic);
        // First ping reaches Connected milestone
        let ping = ConnectionPingInfo::first_connected(start);
        assert!(ping.reaches_new_milestone());

        // One-minute ping reaches OneMinute milestone
        let one_min_ping = ping.next_ping(start + 1.minute());
        assert!(one_min_ping.reaches_new_milestone());

        // Three-minutes ping hasn't reached TenMinutes milestone
        let three_min_ping = one_min_ping.next_ping(start + 3.minutes());
        assert!(!three_min_ping.reaches_new_milestone());

        // Ten-minutes ping reaches TenMinutes milestone
        let ten_min_ping = three_min_ping.next_ping(start + 10.minutes());
        assert!(ten_min_ping.reaches_new_milestone());
    }

    #[test]
    fn test_connection_milestone() {
        assert_eq!(ConnectionMilestone::from_duration(5.seconds()), ConnectionMilestone::Connected);
        assert_eq!(ConnectionMilestone::from_duration(1.minute()), ConnectionMilestone::OneMinute);
        assert_eq!(ConnectionMilestone::from_duration(3.minutes()), ConnectionMilestone::OneMinute);
    }
}
