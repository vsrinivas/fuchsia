// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::bss::BssDescription,
    anyhow, fidl_fuchsia_wlan_sme as fidl_sme,
    std::convert::{TryFrom, TryInto},
};

#[cfg(target_os = "fuchsia")]
use fuchsia_zircon as zx;

#[derive(Debug, Clone, PartialEq)]
pub struct ScanResult {
    pub compatible: bool,
    // Time of the scan result relative to when the system was powered on.
    // See https://fuchsia.dev/fuchsia-src/concepts/time/language_support?hl=en#monotonic_time
    #[cfg(target_os = "fuchsia")]
    pub timestamp: zx::Time,
    pub bss_description: BssDescription,
}

impl From<ScanResult> for fidl_sme::ScanResult {
    fn from(scan_result: ScanResult) -> fidl_sme::ScanResult {
        fidl_sme::ScanResult {
            compatible: scan_result.compatible,
            #[cfg(target_os = "fuchsia")]
            timestamp_nanos: scan_result.timestamp.into_nanos(),
            #[cfg(not(target_os = "fuchsia"))]
            timestamp_nanos: 0,
            bss_description: scan_result.bss_description.into(),
        }
    }
}

impl TryFrom<fidl_sme::ScanResult> for ScanResult {
    type Error = anyhow::Error;

    fn try_from(scan_result: fidl_sme::ScanResult) -> Result<ScanResult, Self::Error> {
        Ok(ScanResult {
            compatible: scan_result.compatible,
            #[cfg(target_os = "fuchsia")]
            timestamp: zx::Time::from_nanos(scan_result.timestamp_nanos),
            bss_description: scan_result.bss_description.try_into()?,
        })
    }
}
