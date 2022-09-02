// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{bss::BssDescription, security::SecurityDescriptor},
    anyhow::{self, format_err},
    fidl_fuchsia_wlan_sme as fidl_sme,
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
    },
};

#[cfg(target_os = "fuchsia")]
use fuchsia_zircon as zx;

/// Compatibility of a BSS with respect to a scanning interface.
///
/// Describes the mutually supported modes of operation between a compatible BSS and a local
/// scanning interface. Here, _compatibility_ refers to the ability to establish a connection.
#[derive(Debug, Clone, PartialEq)]
pub struct Compatibility {
    mutual_security_protocols: HashSet<SecurityDescriptor>,
}

impl Compatibility {
    pub fn try_new(
        mutual_security_protocols: impl IntoIterator<Item = SecurityDescriptor>,
    ) -> Option<Self> {
        let mut mutual_security_protocols = mutual_security_protocols.into_iter();
        let first_security_protocol = mutual_security_protocols.next();
        first_security_protocol.map(|first_security_protocol| Compatibility {
            mutual_security_protocols: Some(first_security_protocol)
                .into_iter()
                .chain(mutual_security_protocols)
                .collect(),
        })
    }

    /// Gets the set of mutually supported security protocols.
    ///
    /// This set represents the intersection of security protocols supported by the BSS and the
    /// scanning interface. In this context, this set is never empty, as that would imply
    /// incompatibility.
    pub fn mutual_security_protocols(&self) -> &HashSet<SecurityDescriptor> {
        &self.mutual_security_protocols
    }
}

impl TryFrom<fidl_sme::Compatibility> for Compatibility {
    type Error = ();

    fn try_from(compatibility: fidl_sme::Compatibility) -> Result<Self, Self::Error> {
        let fidl_sme::Compatibility { mutual_security_protocols } = compatibility;
        Compatibility::try_new(mutual_security_protocols.into_iter().map(From::from)).ok_or(())
    }
}

impl From<Compatibility> for fidl_sme::Compatibility {
    fn from(compatibility: Compatibility) -> Self {
        let Compatibility { mutual_security_protocols } = compatibility;
        fidl_sme::Compatibility {
            mutual_security_protocols: mutual_security_protocols
                .into_iter()
                .map(From::from)
                .collect(),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ScanResult {
    pub compatibility: Option<Compatibility>,
    // Time of the scan result relative to when the system was powered on.
    // See https://fuchsia.dev/fuchsia-src/concepts/time/language_support?hl=en#monotonic_time
    #[cfg(target_os = "fuchsia")]
    pub timestamp: zx::Time,
    pub bss_description: BssDescription,
}

impl ScanResult {
    pub fn is_compatible(&self) -> bool {
        self.compatibility.is_some()
    }
}

impl From<ScanResult> for fidl_sme::ScanResult {
    fn from(scan_result: ScanResult) -> fidl_sme::ScanResult {
        let ScanResult {
            compatibility,
            #[cfg(target_os = "fuchsia")]
            timestamp,
            bss_description,
        } = scan_result;
        fidl_sme::ScanResult {
            compatibility: compatibility.map(From::from).map(Box::new),
            #[cfg(target_os = "fuchsia")]
            timestamp_nanos: timestamp.into_nanos(),
            #[cfg(not(target_os = "fuchsia"))]
            timestamp_nanos: 0,
            bss_description: bss_description.into(),
        }
    }
}

impl TryFrom<fidl_sme::ScanResult> for ScanResult {
    type Error = anyhow::Error;

    fn try_from(scan_result: fidl_sme::ScanResult) -> Result<ScanResult, Self::Error> {
        #[allow(unused_variables)]
        let fidl_sme::ScanResult { compatibility, timestamp_nanos, bss_description } = scan_result;
        Ok(ScanResult {
            compatibility: compatibility
                .map(|compatibility| *compatibility)
                .map(TryFrom::try_from)
                .transpose()
                .map_err(|_| format_err!("failed to convert FIDL `Compatibility`"))?,
            #[cfg(target_os = "fuchsia")]
            timestamp: zx::Time::from_nanos(timestamp_nanos),
            bss_description: bss_description.try_into()?,
        })
    }
}
