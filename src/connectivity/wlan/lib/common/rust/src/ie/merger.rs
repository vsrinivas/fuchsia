// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{Header, IeSummaryIter, IeType},
    anyhow::format_err,
    std::{collections::BTreeMap, mem::size_of, ops::Range},
};

const IES_MERGER_BUFFER_LIMIT: usize = 10000;

/// IesMerger is intended to be used to merge beacon and probe response IEs from scan results, as
/// some IEs are only available on one frame type, or an IE may exist on both but contains more
/// information on one frame type over the other.
///
/// IesMerger therefore has two purposes:
/// 1. Combine unique IEs from each one into a single IE block.
/// 2. Pick which IE to keep when the same type is available in multiple IE blocks but has
///    different information:
///    a. IesMerger knows about and uses custom logic for specific IE (e.g. for SSID, we
///       prioritize non-hidden SSID over hidden SSID).
///    b. In the general case, IesMerger keeps the IE that has more bytes (known use case:
///       for WPS IE, the one in probe response contains more information).
///    c. If the number of bytes are the same, IesMerger keeps the later one.
#[derive(Debug)]
pub struct IesMerger {
    ies_updater: IesUpdater,
    buffer_overflow: bool,
    merge_ie_failures: usize,
}

impl IesMerger {
    pub fn new(ies: Vec<u8>) -> Self {
        Self { ies_updater: IesUpdater::new(ies), buffer_overflow: false, merge_ie_failures: 0 }
    }

    pub fn merge(&mut self, ies: &[u8]) {
        for (ie_type, range) in IeSummaryIter::new(ies) {
            let add_new = match self.ies_updater.get(&ie_type) {
                Some(old) => should_add_new(ie_type, old, &ies[range.clone()]),
                None => true,
            };
            let new_addition_len = range.end - range.start;
            // IesMerger has a buffer limit so an unfriendly AP can't cause us to run out of
            // memory by repeatedly changing the IEs.
            if add_new {
                if self.ies_updater.buf_len() + new_addition_len <= IES_MERGER_BUFFER_LIMIT {
                    // Setting IE should not fail because we parsed them from an IE chain in the
                    // first place, so the length of the IE body would not exceed 255 bytes.
                    if let Err(_e) = self.ies_updater.set(ie_type, &ies[range]) {
                        self.merge_ie_failures += 1;
                    }
                } else {
                    self.buffer_overflow = true;
                }
            }
        }
    }

    /// Build and return merged IEs, sorted by order of IeType.
    pub fn finalize(&mut self) -> Vec<u8> {
        self.ies_updater.finalize()
    }

    /// Return a bool indicating whether an IE was not merged because it would have exceeded
    /// IesMerger's buffer.
    pub fn buffer_overflow(&mut self) -> bool {
        self.buffer_overflow
    }

    /// Return the number of times IesMerger decides to merge an IE but it fails to.
    /// This should never occur, but we keep a counter in case the assumption is violated.
    pub fn merge_ie_failures(&self) -> usize {
        self.merge_ie_failures
    }
}

#[derive(Debug)]
pub struct IesUpdater {
    // An index of the IEs we are going to keep. Mapping from IeType to the range of the
    // corresponding bytes in the underlying buffer.
    ies_summaries: BTreeMap<IeType, Range<usize>>,
    ies_buf: Vec<u8>,
}

impl IesUpdater {
    pub fn new(ies: Vec<u8>) -> Self {
        let mut ies_summaries = BTreeMap::new();
        for (ie_type, range) in IeSummaryIter::new(&ies[..]) {
            ies_summaries.insert(ie_type, range);
        }
        Self { ies_summaries, ies_buf: ies }
    }

    /// Remove any IE with the corresponding `ie_type`.
    pub fn remove(&mut self, ie_type: &IeType) {
        self.ies_summaries.remove(ie_type);
    }

    /// Set an IE with the corresponding `ie_type`, replacing any existing entry with the same type.
    /// The IE is rejected if it's too large.
    pub fn set(&mut self, ie_type: IeType, ie_content: &[u8]) -> Result<(), anyhow::Error> {
        // If length of this IE is too large, ignore it because later on we cannot construct the
        // IE anyway on `finalize`.
        if ie_type.extra_len() + ie_content.len() > std::u8::MAX.into() {
            return Err(format_err!("ie_content too large"));
        }

        let start_idx = self.ies_buf.len();
        self.ies_buf.extend_from_slice(ie_content);
        self.ies_summaries.insert(ie_type, start_idx..self.ies_buf.len());
        Ok(())
    }

    /// Set the raw IE (including the IE header).
    /// The IE is rejected if the IE's length field (denoted by the second byte) is larger than
    /// the length of the remaining IE bytes, and truncated if it's less than.
    pub fn set_raw(&mut self, ie: &[u8]) -> Result<(), anyhow::Error> {
        if ie.is_empty() {
            return Ok(());
        }
        match IeSummaryIter::new(&ie[..]).next() {
            Some((ie_type, range)) => self.set(ie_type, &ie[range]),
            None => Err(format_err!("failed parsing `ie`")),
        }
    }

    /// Get the IE bytes of the given `ie_type`, if it exists.
    pub fn get(&self, ie_type: &IeType) -> Option<&[u8]> {
        self.ies_summaries.get(ie_type).map(|range| &self.ies_buf[range.clone()])
    }

    fn buf_len(&self) -> usize {
        self.ies_buf.len()
    }

    /// Build and return the modified IEs, sorted by order of IeType.
    pub fn finalize(&mut self) -> Vec<u8> {
        let total_len = self
            .ies_summaries
            .iter()
            .map(|(ie_type, r)| size_of::<Header>() + ie_type.extra_len() + (r.end - r.start))
            .sum();
        let mut ies = Vec::with_capacity(total_len);
        for (ie_type, range) in self.ies_summaries.iter() {
            let id = ie_type.basic_id().0;
            let len = ie_type.extra_len() + (range.end - range.start);
            // Casting `len` to u8 is safe because in `set`, we reject any IE that is too large
            ies.extend_from_slice(&[id, len as u8]);
            ies.extend_from_slice(ie_type.extra_bytes());
            ies.extend_from_slice(&self.ies_buf[range.clone()]);
        }
        ies
    }
}

fn should_add_new(ie_type: IeType, old: &[u8], new: &[u8]) -> bool {
    // If both IEs are the same, no need to add new IE
    if old == new {
        return false;
    }

    // If the new SSID is blank (hidden SSID), prefer the old one
    if ie_type == IeType::SSID {
        if new.len() < size_of::<Header>() {
            return false;
        }
        let ssid = &new[2..];
        if ssid.iter().all(|c| *c == 0u8) {
            return false;
        }
    }

    // In general case, prioritize the one with more information.
    // For example, for WPS vendor IE, the AP sends a short one in the beacon and
    // a longer one in the probe response.
    //
    // If they are the same length but different, then prioritize the new one.
    new.len() >= old.len()
}

#[cfg(test)]
mod tests {
    use super::*;

    // The beacon IEs here and the probe response IEs below are taken from the same router, with
    // the following modification:
    // 1. SSID is changed
    // 2. Order of IEs are sorted according to `IeType` ordering to make test code simpler.
    #[rustfmt::skip]
    const BEACON_FRAME_IES: &'static [u8] = &[
        // SSID: "foo-ssid"
        0x00, 0x08, 0x66, 0x6f, 0x6f, 0x2d, 0x73, 0x73, 0x69, 0x64,
        // Supported Rates: 6(B), 9, 12(B), 18, 24(B), 36, 48, 54, [Mbit/sec]
        0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
        // DS Parameter set: Current Channel: 157
        0x03, 0x01, 0x9d,
        // Traffic Indication Map (TIM): DTIM 0 of 0 bitmap
        0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
        // Power Constraint: 3
        0x20, 0x01, 0x03,
        // HT Capabilities (802.11n D1.10)
        0x2d, 0x1a,
        0xef, 0x09, // HT Capabilities Info
        0x1b, // A-MPDU Parameters: 0x1b
        0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // MCS Set
        0x00, 0x00, // HT Extended Capabilities
        0x00, 0x00, 0x00, 0x00, // Transmit Beamforming Capabilities
        0x00, // Antenna Selection Capabilities
        // RSN Information
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04, // Group Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, // AKM: PSK
        0x00, 0x00, // RSN Capabilities
        // HT Information (802.11n D1.10)
        0x3d, 0x16,
        0x9d, // Primary Channel: 157
        0x0d, // HT Info Subset - secondary channel above, any channel width, RIFS permitted
        0x00, 0x00, 0x00, 0x00, // HT Info Subsets
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS Set
        // Overlapping BSS Scan Parameters
        0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
        // Extended Capabilities
        0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
        // VHT Capabilities
        0xbf, 0x0c,
        0xb2, 0x01, 0x80, 0x33, // VHT Capabilities Info
        0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT Supported MCS Set
        // VHT Operation
        0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
        // VHT Tx Power Envelope
        0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
        // Vendor Specific: Atheros Communications, Inc.: Advanced Capability
        0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
        // Vendor Specific: Microsoft Corp.: WMM/WME: Parameter Element
        0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
        0x80, // U-APSD enabled
        0x00,
        0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
        0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
        0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
        0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        // Vendor Specific: Microsoft Corp.: WPS
        0xdd, 0x1d, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01,
        0x02, 0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01,
        0x20,
    ];

    #[rustfmt::skip]
    const PROBE_RESP_IES: &'static [u8] = &[
        // SSID: "foo-ssid"
        0x00, 0x08, 0x66, 0x6f, 0x6f, 0x2d, 0x73, 0x73, 0x69, 0x64,
        // Supported Rates: 6(B), 9, 12(B), 18, 24(B), 36, 48, 54, [Mbit/sec]
        0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
        // DS Parameter set: Current Channel: 157
        0x03, 0x01, 0x9d,
        // Power Constraint: 3
        0x20, 0x01, 0x03,
        // HT Capabilities (802.11n D1.10)
        0x2d, 0x1a,
        0xef, 0x09, // HT Capabilities Info
        0x1b, // A-MPDU Parameters: 0x1b
        0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // MCS Set
        0x00, 0x00, // HT Extended Capabilities
        0x00, 0x00, 0x00, 0x00, // Transmit Beamforming Capabilities
        0x00, // Antenna Selection Capabilities
        // RSN Information
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04, // Group Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, // AKM: PSK
        0x00, 0x00, // RSN Capabilities
        // HT Information (802.11n D1.10)
        0x3d, 0x16,
        0x9d, // Primary Channel: 157
        0x0d, // HT Info Subset - secondary channel above, any channel width, RIFS permitted
        0x00, 0x00, 0x00, 0x00, // HT Info Subsets
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS Set
        // Overlapping BSS Scan Parameters
        0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
        // Extended Capabilities
        0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
        // VHT Capabilities
        0xbf, 0x0c,
        0xb2, 0x01, 0x80, 0x33, // VHT Capabilities Info
        0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT Supported MCS Set
        // VHT Operation
        0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
        // VHT Tx Power Envelope
        0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
        // Vendor Specific: Atheros Communications, Inc.: Advanced Capability
        0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
        // Vendor Specific: Microsoft Corp.: WMM/WME: Parameter Element
        0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
        0x80, // U-APSD enabled
        0x00,
        0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
        0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
        0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
        0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        // Vendor Specific: Microsoft Corp.: WPS
        0xdd, 0x85, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01,
        0x02, 0x10, 0x3b, 0x00, 0x01, 0x03, 0x10, 0x47, 0x00, 0x10, 0x87, 0x65, 0x43, 0x21, 0x9a,
        0xbc, 0xde, 0xf0, 0x12, 0x34, 0x98, 0xda, 0xc4, 0x8e, 0x86, 0xc8, 0x10, 0x21, 0x00, 0x07,
        0x54, 0x50, 0x2d, 0x4c, 0x69, 0x6e, 0x6b, 0x10, 0x23, 0x00, 0x09, 0x41, 0x72, 0x63, 0x68,
        0x65, 0x72, 0x20, 0x41, 0x37, 0x10, 0x24, 0x00, 0x03, 0x31, 0x2e, 0x30, 0x10, 0x42, 0x00,
        0x0c, 0x41, 0x72, 0x63, 0x68, 0x65, 0x72, 0x20, 0x41, 0x37, 0x20, 0x76, 0x35, 0x10, 0x54,
        0x00, 0x08, 0x00, 0x06, 0x00, 0x50, 0xf2, 0x04, 0x00, 0x01, 0x10, 0x11, 0x00, 0x0a, 0x41,
        0x72, 0x63, 0x68, 0x65, 0x72, 0x41, 0x37, 0x76, 0x35, 0x10, 0x08, 0x00, 0x02, 0x00, 0x04,
        0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20,
    ];

    #[rustfmt::skip]
    const MERGED_IES: &'static [u8] = &[
        // SSID: "foo-ssid"
        0x00, 0x08, 0x66, 0x6f, 0x6f, 0x2d, 0x73, 0x73, 0x69, 0x64,
        // Supported Rates: 6(B), 9, 12(B), 18, 24(B), 36, 48, 54, [Mbit/sec]
        0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
        // DS Parameter set: Current Channel: 157
        0x03, 0x01, 0x9d,
        // Traffic Indication Map (TIM): DTIM 0 of 0 bitmap
        0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
        // Power Constraint: 3
        0x20, 0x01, 0x03,
        // HT Capabilities (802.11n D1.10)
        0x2d, 0x1a,
        0xef, 0x09, // HT Capabilities Info
        0x1b, // A-MPDU Parameters: 0x1b
        0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // MCS Set
        0x00, 0x00, // HT Extended Capabilities
        0x00, 0x00, 0x00, 0x00, // Transmit Beamforming Capabilities
        0x00, // Antenna Selection Capabilities
        // RSN Information
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04, // Group Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, // AKM: PSK
        0x00, 0x00, // RSN Capabilities
        // HT Information (802.11n D1.10)
        0x3d, 0x16,
        0x9d, // Primary Channel: 157
        0x0d, // HT Info Subset - secondary channel above, any channel width, RIFS permitted
        0x00, 0x00, 0x00, 0x00, // HT Info Subsets
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS Set
        // Overlapping BSS Scan Parameters
        0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
        // Extended Capabilities
        0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
        // VHT Capabilities
        0xbf, 0x0c,
        0xb2, 0x01, 0x80, 0x33, // VHT Capabilities Info
        0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT Supported MCS Set
        // VHT Operation
        0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
        // VHT Tx Power Envelope
        0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
        // Vendor Specific: Atheros Communications, Inc.: Advanced Capability
        0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
        // Vendor Specific: Microsoft Corp.: WMM/WME: Parameter Element
        0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
        0x80, // U-APSD enabled
        0x00,
        0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
        0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
        0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
        0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        // Vendor Specific: Microsoft Corp.: WPS
        0xdd, 0x85, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01,
        0x02, 0x10, 0x3b, 0x00, 0x01, 0x03, 0x10, 0x47, 0x00, 0x10, 0x87, 0x65, 0x43, 0x21, 0x9a,
        0xbc, 0xde, 0xf0, 0x12, 0x34, 0x98, 0xda, 0xc4, 0x8e, 0x86, 0xc8, 0x10, 0x21, 0x00, 0x07,
        0x54, 0x50, 0x2d, 0x4c, 0x69, 0x6e, 0x6b, 0x10, 0x23, 0x00, 0x09, 0x41, 0x72, 0x63, 0x68,
        0x65, 0x72, 0x20, 0x41, 0x37, 0x10, 0x24, 0x00, 0x03, 0x31, 0x2e, 0x30, 0x10, 0x42, 0x00,
        0x0c, 0x41, 0x72, 0x63, 0x68, 0x65, 0x72, 0x20, 0x41, 0x37, 0x20, 0x76, 0x35, 0x10, 0x54,
        0x00, 0x08, 0x00, 0x06, 0x00, 0x50, 0xf2, 0x04, 0x00, 0x01, 0x10, 0x11, 0x00, 0x0a, 0x41,
        0x72, 0x63, 0x68, 0x65, 0x72, 0x41, 0x37, 0x76, 0x35, 0x10, 0x08, 0x00, 0x02, 0x00, 0x04,
        0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20,
    ];

    #[test]
    fn test_no_merge() {
        let ies = IesMerger::new(BEACON_FRAME_IES.to_vec()).finalize();
        assert_eq!(&ies[..], BEACON_FRAME_IES);
    }

    #[test]
    fn test_merge_same_ies() {
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(BEACON_FRAME_IES);
        let ies = ies_merger.finalize();
        assert_eq!(&ies[..], BEACON_FRAME_IES);
        assert!(!ies_merger.buffer_overflow());
    }

    #[test]
    fn test_merge_different_ies() {
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(PROBE_RESP_IES);
        let ies = ies_merger.finalize();
        assert_eq!(&ies[..], MERGED_IES);
        assert!(!ies_merger.buffer_overflow());
    }

    // Verify that merging IEs in a different order should still produce the same result
    // (this is not true in the general case, but is true with the data we mock)
    #[test]
    fn test_merge_different_ies_commutative() {
        let mut ies_merger1 = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger1.merge(PROBE_RESP_IES);
        let ies1 = ies_merger1.finalize();

        let mut ies_merger2 = IesMerger::new(PROBE_RESP_IES.to_vec());
        ies_merger2.merge(BEACON_FRAME_IES);
        let ies2 = ies_merger2.finalize();

        assert_eq!(ies1, ies2);
    }

    // In the course of a scan, we may merge the same thing multiple times.
    // This tests such use case.
    #[test]
    fn test_merge_redundant() {
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(BEACON_FRAME_IES);
        ies_merger.merge(PROBE_RESP_IES);
        ies_merger.merge(PROBE_RESP_IES);
        ies_merger.merge(BEACON_FRAME_IES);
        let ies = ies_merger.finalize();

        assert_eq!(&ies[..], MERGED_IES);
    }

    // Test that we don't overuse memory if an unfriendly AP repeatedly updates the IEs.
    #[test]
    fn test_merge_is_resilient() {
        const LEN: u8 = 255;
        let mut ies = vec![0xdd, LEN];
        ies.extend(vec![0xff; LEN as usize].iter());

        let mut ies_merger = IesMerger::new(ies.clone());
        for i in 0..255 {
            let last_idx = ies.len() - 1;
            ies[last_idx] = i; // Tweak one value so the IEs is different, forcing a merge.
            ies_merger.merge(&ies[..]);
        }
        // Verify we don't use too much memory.
        assert!(ies_merger.ies_updater.buf_len() <= IES_MERGER_BUFFER_LIMIT);
        // Verify buffer overflow flag is set to true.
        assert!(ies_merger.buffer_overflow());

        // We should still produce a result.
        let result_ies = ies_merger.finalize();
        assert_eq!(ies.len(), result_ies.len());
        // Verify one IE picked (whichever IE is picked, all but the last byte are the same).
        assert_eq!(ies[..ies.len() - 1], result_ies[..result_ies.len() - 1]);
    }

    #[test]
    fn test_merge_prioritize_non_hidden_ssid_1() {
        let hidden_ssid_ie = vec![0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(&hidden_ssid_ie[..]);
        let ies = ies_merger.finalize();

        // The non-hidden SSID IE is kept.
        assert_eq!(&ies[..], BEACON_FRAME_IES);

        // Sanity check by initializing with hidden SSID first.
        let mut ies_merger = IesMerger::new(hidden_ssid_ie.clone());
        // This verifies that hidden SSID IE is kept if it's the only one.
        assert_eq!(ies_merger.finalize(), hidden_ssid_ie);

        let mut ies_merger = IesMerger::new(hidden_ssid_ie);
        ies_merger.merge(BEACON_FRAME_IES);
        let ies = ies_merger.finalize();

        // The non-hidden SSID IE is kept.
        assert_eq!(&ies[..], BEACON_FRAME_IES);
    }

    #[test]
    fn test_ie_updater_get() {
        let ies = vec![
            0, 2, 10, 20, // IE with no extension ID
            0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
            255, 2, 5, 1, // IE with extension ID
        ];
        let ies_updater = IesUpdater::new(ies);

        assert_eq!(ies_updater.get(&IeType::SSID), Some(&[10, 20][..]));
        assert_eq!(
            ies_updater.get(&IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x00])),
            Some(&[0x00, 0xff, 0x7f][..])
        );
        assert_eq!(ies_updater.get(&IeType::new_extended(5)), Some(&[1][..]));

        assert_eq!(ies_updater.get(&IeType::SUPPORTED_RATES), None);
        assert_eq!(
            ies_updater.get(&IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x01])),
            None
        );
        assert_eq!(ies_updater.get(&IeType::new_extended(6)), None);
    }

    #[test]
    fn test_ie_updater_set_replace() {
        let ies = vec![
            0, 2, 10, 20, // IE with no extension ID
            0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
            255, 2, 5, 1, // IE with extension ID
        ];
        let mut ies_updater = IesUpdater::new(ies);

        ies_updater.set(IeType::SSID, &[30, 40, 50]).expect("set basic succeeds");
        ies_updater
            .set(IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x00]), &[1, 3, 3, 7])
            .expect("set vendor succeeds");
        ies_updater.set(IeType::new_extended(5), &[4, 2]).expect("set extended succeeds");

        assert_eq!(ies_updater.get(&IeType::SSID), Some(&[30, 40, 50][..]));
        assert_eq!(
            ies_updater.get(&IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x00])),
            Some(&[1, 3, 3, 7][..])
        );
        assert_eq!(ies_updater.get(&IeType::new_extended(5)), Some(&[4, 2][..]));

        assert_eq!(
            &ies_updater.finalize()[..],
            &[
                0, 3, 30, 40, 50, // IE with no extension ID
                0xdd, 0x0a, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 1, 3, 3, 7, // Vendor IE
                255, 3, 5, 4, 2, // IE with extension ID
            ]
        );
    }

    #[test]
    fn test_ie_updater_set_new() {
        let ies = vec![
            0, 2, 10, 20, // IE with no extension ID
            0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
            255, 2, 5, 1, // IE with extension ID
        ];
        let mut ies_updater = IesUpdater::new(ies);

        ies_updater.set(IeType::SUPPORTED_RATES, &[30, 40, 50]).expect("set basic succeeds");
        ies_updater
            .set(IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x01]), &[1, 3, 3, 7])
            .expect("set vendor succeeds");
        ies_updater.set(IeType::new_extended(6), &[4, 2]).expect("set extended succeeds");

        assert_eq!(ies_updater.get(&IeType::SUPPORTED_RATES), Some(&[30, 40, 50][..]));
        assert_eq!(
            ies_updater.get(&IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x01])),
            Some(&[1, 3, 3, 7][..])
        );
        assert_eq!(ies_updater.get(&IeType::new_extended(6)), Some(&[4, 2][..]));

        assert_eq!(
            &ies_updater.finalize()[..],
            &[
                0, 2, 10, 20, // IE with no extension ID
                1, 3, 30, 40, 50, // New IE with no extension ID
                0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
                0xdd, 0x0a, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x01, 1, 3, 3, 7, // New vendor IE
                255, 2, 5, 1, // IE with extension ID
                255, 3, 6, 4, 2, // New IE with extension ID
            ]
        )
    }

    #[test]
    fn test_ie_updater_set_ie_too_large() {
        let ies = vec![
            0, 2, 10, 20, // IE with no extension ID
            0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
            255, 2, 5, 1, // IE with extension ID
        ];
        let mut ies_updater = IesUpdater::new(ies);
        ies_updater.set(IeType::SSID, &[11; 256]).expect_err("set basic fails");
        ies_updater
            .set(IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x00]), &[11; 250])
            .expect_err("set vendor fails");
        ies_updater.set(IeType::new_extended(5), &[11; 255]).expect_err("set extended fails");

        // None of the IEs got replaced because all the IEs set in this test are too large
        assert_eq!(
            &ies_updater.finalize()[..],
            &[
                0, 2, 10, 20, // IE with no extension ID
                0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
                255, 2, 5, 1, // IE with extension ID
            ]
        );
    }

    #[test]
    fn test_ie_updater_set_raw_ie() {
        let ies = vec![];

        let mut ies_updater = IesUpdater::new(ies.clone());
        ies_updater.set_raw(&[0, 2, 10, 20]).expect("set right length succeeds");
        ies_updater.set_raw(&[1, 2, 70]).expect_err("set buffer too small fails");
        ies_updater.set_raw(&[]).expect("set empty doesn't return error");
        ies_updater.set_raw(&[2, 2, 30, 40, 50, 60]).expect("set truncated succeeds");

        assert_eq!(&ies_updater.finalize()[..], &[0, 2, 10, 20, 2, 2, 30, 40])
    }

    #[test]
    fn test_ie_updater_remove() {
        let ies = vec![
            0, 2, 10, 20, // IE with no extension ID
            0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
            255, 2, 5, 1, // IE with extension ID
        ];

        let mut ies_updater = IesUpdater::new(ies.clone());
        ies_updater.remove(&IeType::SSID);
        assert_eq!(ies_updater.get(&IeType::SSID), None);
        assert_eq!(
            ies_updater.finalize(),
            &[
                0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
                255, 2, 5, 1, // IE with extension ID
            ]
        );

        let mut ies_updater = IesUpdater::new(ies.clone());
        ies_updater.remove(&IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x00]));
        assert_eq!(
            ies_updater.get(&IeType::new_vendor([0x00, 0x03, 0x7f, 0x01, 0x01, 0x00])),
            None
        );
        assert_eq!(
            ies_updater.finalize(),
            &[
                0, 2, 10, 20, // IE with no extension ID
                255, 2, 5, 1, // IE with extension ID
            ],
        );

        let mut ies_updater = IesUpdater::new(ies);
        ies_updater.remove(&IeType::new_extended(5));
        assert_eq!(ies_updater.get(&IeType::new_extended(5)), None);
        assert_eq!(
            ies_updater.finalize(),
            &[
                0, 2, 10, 20, // IE with no extension ID
                0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f, // Vendor IE
            ],
        );
    }
}
