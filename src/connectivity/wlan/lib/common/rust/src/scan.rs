// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        bss::{BssDescription, Protection},
        channel::Channel,
        mac::CapabilityInfo,
        Bssid,
    },
    anyhow, fidl_fuchsia_wlan_internal as fidl_internal, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_zircon as zx,
    ieee80211::Ssid,
    std::convert::{TryFrom, TryInto},
};

#[derive(Debug, Clone, PartialEq)]
pub struct ScanResult {
    pub compatible: bool,
    pub timestamp: zx::Time,
    pub bss_description: BssDescription,
}

impl From<&ScanResult> for fidl_sme::ScanResult {
    fn from(scan_result: &ScanResult) -> fidl_sme::ScanResult {
        fidl_sme::ScanResult {
            compatible: scan_result.compatible,
            timestamp_nanos: scan_result.timestamp.into_nanos(),
            bss_description: scan_result.bss_description.clone().into(),
        }
    }
}

impl From<ScanResult> for fidl_sme::ScanResult {
    fn from(scan_result: ScanResult) -> fidl_sme::ScanResult {
        fidl_sme::ScanResult {
            compatible: scan_result.compatible,
            timestamp_nanos: scan_result.timestamp.into_nanos(),
            bss_description: scan_result.bss_description.into(),
        }
    }
}

impl TryFrom<fidl_sme::ScanResult> for ScanResult {
    type Error = anyhow::Error;

    fn try_from(scan_result: fidl_sme::ScanResult) -> Result<ScanResult, Self::Error> {
        Ok(ScanResult {
            compatible: scan_result.compatible,
            timestamp: zx::Time::from_nanos(scan_result.timestamp_nanos),
            bss_description: scan_result.bss_description.try_into()?,
        })
    }
}

impl TryFrom<&fidl_sme::ScanResult> for ScanResult {
    type Error = anyhow::Error;

    fn try_from(scan_result: &fidl_sme::ScanResult) -> Result<ScanResult, Self::Error> {
        Ok(ScanResult {
            compatible: scan_result.compatible,
            timestamp: zx::Time::from_nanos(scan_result.timestamp_nanos),
            bss_description: scan_result.bss_description.clone().try_into()?,
        })
    }
}

impl ScanResult {
    pub fn ssid<'a>(&'a self) -> &'a Ssid {
        &self.bss_description.ssid
    }
    pub fn bssid<'a>(&'a self) -> &'a Bssid {
        &self.bss_description.bssid
    }
    pub fn bss_type(&self) -> fidl_internal::BssType {
        self.bss_description.bss_type
    }
    pub fn beacon_period(&self) -> u16 {
        self.bss_description.beacon_period
    }
    pub fn capability_info(&self) -> CapabilityInfo {
        CapabilityInfo(self.bss_description.capability_info)
    }
    pub fn channel(&self) -> Channel {
        self.bss_description.channel.clone().into()
    }
    pub fn rssi_dbm(&self) -> i8 {
        self.bss_description.rssi_dbm
    }
    pub fn snr_db(&self) -> i8 {
        self.bss_description.snr_db
    }
    pub fn protection(&self) -> Protection {
        self.bss_description.protection()
    }
}
