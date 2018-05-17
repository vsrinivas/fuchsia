// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use std::time::Duration;

pub enum ScanType {
    Active,
    Passive,
}

pub enum BssType {
    Infrastructure,
    Personal,
    Independent,
    Mesh,
    AnyBss,
}

/// MLME-SCAN.request (IEEE Std 802.11-2016 6.3.3.2)
/// This primitive requests a survey of potential BSSs that the STA can later elect to try to join.
pub struct Request {
    /// Determines whether infrastructure BSS, PBSS, IBSS, MBSS, or all, are included in the scan.
    pub bss_type: BssType,
    /// Identifies a specific or wildcard BSSID.
    pub bssid: Option<Bssid>,
    /// Specifies the desired SSID or the wildcard SSID.
    pub ssid: Option<Ssid>,
    /// Indicates either active or passive scanning.
    pub scan_type: ScanType,
    /// Delay (in microseconds) to be used prior to transmitting a Probe frame during active
    /// scanning.
    pub probe_delay: Option<Duration>,
    /// Specifies a list of channels that are examined when scanning for a BSS.
    pub channel_list: Vec<ChannelNumber>,
    /// The minimum time (in TU) to spend on each channel when scanning.
    pub min_channel_time: Option<Duration>,
    /// The maximum time (in TU) to spend on each channel when scanning.
    pub max_channel_time: Option<Duration>,
    /// One or more SSID elements that are optionally present when dot11SSIDListActivated is true.
    pub ssid_list: Option<Vec<Ssid>>,
}

/// Extension - Specific interpretation of certain attributes of a BSSDescription
pub struct Extension {
    pub primary: ChannelNumber,
    pub rssi_dbm: i8,
}

pub struct BssDescription {
    /// The BSSID of the found BSS or the MAC address of the found mesh STA.
    pub bssid: Bssid,
    /// The SSID of the found BSS.
    pub ssid: Ssid,
    /// The type of the found BSS.
    pub bss_type: BssType,
    /// The beacon period (in TU) of the found BSS if the BSSType is not MESH, or of the found
    /// mesh STA if the BSSType = MESH.
    pub beacon_period: u32,
    /// The DTIM period (in beacon periods) of the BSS if the BSSType is not MESH, or of the mesh
    /// STA if the BSSType = MESH.
    pub dtim_period: u32,
    /// The timestamp of the received frame (Probe Response/ Beacon) from the found BSS.
    pub timestamp: u64,
    /// The value of the local STA’s TSF timer at the start of reception of the first octet of the
    /// timestamp field of the received frame (Probe Response or Beacon) from the found BSS.
    pub local_time: u64,
    /// A description of the cipher suites and AKM suites supported in the BSS.
    pub rsn: Option<Vec<u8>>,
    /// The RCPI of the received frame.
    pub rcpi_dbmh: i16,
    /// The RSNI of the received frame.
    pub rsni_dbh: i16,

    // Not in the standard
    pub extension: Extension,
}

pub enum ResultCode {
    Success,
    NotSupported,
}

/// MLME-SCAN.confirm (IEEE Std 802.11-2016 6.3.3.3)
pub struct Confirm {
    /// The BSSDescriptionSet is returned to indicate the results of the scan request. It is a set
    /// containing zero or more instances of a BSSDescription.
    pub bss_description_set: Vec<BssDescription>,
    /// Indicates the result of the MLME- SCAN.confirm primitive.
    pub result_code: ResultCode,
}
